#include "glass.h"

#include <math.h>

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float smooth(float t)
{
    t = clampf(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float settle(float t)
{
    float s = smooth(t);
    return s * s;
}

static const float R_REST = 118.f;
static const float R_FULL = 206.f;
static const float R_SIGH = 218.f;

void glass_look_clear(glass_look *out)
{
    out->radius = R_REST;
    out->radius_lag = R_REST;
    out->radius_second = R_REST;
    out->second = 0.f;
    out->amp = 0.85f;
    out->mote = 0.35f;
    out->warmth = 0.f;
    out->disk = 0.f;
    out->sway = 0.f;
    out->sway_y = 0.f;
    out->gather = 0.f;
    out->bead = 0.f;
    out->bead_angle = 0.f;
    out->bead_free = 0;
    out->bead_x = 0.f;
    out->bead_y = 0.f;
    out->light_angle = 0.f;
    out->arc = 0.f;
    out->marks = 0.f;
    out->marks_lit = 0.f;
    out->numeral = 0;
    out->numeral_light = 0.f;
    out->lamp = 0.f;
    out->rune = 0.f;
    out->cell = 1.f;
    out->bare = 0;
    out->guiding = 0;
    out->mark_x = 233.f;
    out->mark_y = 233.f;
}

static void tide_shape(uint32_t ms, float *radius, float *openness)
{
    uint32_t p = ms % GLASS_TIDE_MS;

    if (p < GLASS_TIDE_IN_MS) {
        float u = smooth((float)p / (float)GLASS_TIDE_IN_MS);
        *radius = lerpf(R_REST, R_FULL, u);
        *openness = u;
    } else {
        float u = settle((float)(p - GLASS_TIDE_IN_MS) / (float)(GLASS_TIDE_MS - GLASS_TIDE_IN_MS));
        *radius = lerpf(R_FULL, R_REST, u);
        *openness = 1.f - u;
    }
}

static void paint_breath(uint32_t ms, float radius, float lag, float openness, glass_look *out)
{
    glass_look_clear(out);
    out->radius = radius;
    out->radius_lag = lag;
    out->amp = 0.84f + 0.16f * openness;
    out->mote = 0.18f + 0.82f * openness;
    out->light_angle = (float)ms * 0.00012f;
}

void glass_look_tide(uint32_t ms, glass_look *out)
{
    float radius;
    float openness;
    float lag_r;
    float lag_o;

    tide_shape(ms, &radius, &openness);
    tide_shape(ms >= 220u ? ms - 220u : 0u, &lag_r, &lag_o);
    paint_breath(ms, radius, lag_r, openness, out);
}

static void walk_shape(uint32_t ms, float *radius, float *openness)
{
    const uint32_t period = 5600u;
    const uint32_t inhale = 2240u;
    uint32_t p = ms % period;

    if (p < inhale) {
        float u = smooth((float)p / (float)inhale);
        *radius = lerpf(R_REST, R_FULL, u);
        *openness = u;
    } else {
        float u = settle((float)(p - inhale) / (float)(period - inhale));
        *radius = lerpf(R_FULL, R_REST, u);
        *openness = 1.f - u;
    }
}

void glass_look_walk(uint32_t ms, glass_look *out)
{
    float radius;
    float openness;
    float lag_r;
    float lag_o;

    walk_shape(ms, &radius, &openness);
    walk_shape(ms >= 180u ? ms - 180u : 0u, &lag_r, &lag_o);
    paint_breath(ms, radius, lag_r, openness, out);
}

void glass_look_sigh(uint32_t elapsed_ms, glass_look *out)
{
    uint32_t cycle = elapsed_ms % GLASS_SIGH_CYCLE_MS;
    uint32_t at = cycle;
    float radius = R_REST;
    float openness = 0.15f;
    float second = 0.f;
    float radius_second = R_REST;

    if (at < GLASS_SIGH_IN_MS) {
        float u = smooth((float)at / (float)GLASS_SIGH_IN_MS);
        radius = lerpf(R_REST, 188.f, u);
        openness = 0.35f + 0.55f * u;
    } else if (at < GLASS_SIGH_IN_MS + GLASS_SIGH_TOP_MS) {
        float u = smooth((float)(at - GLASS_SIGH_IN_MS) / (float)GLASS_SIGH_TOP_MS);
        radius = lerpf(188.f, R_SIGH, u);
        second = sinf(u * 3.1415926f);
        radius_second = radius * (1.07f - 0.04f * u);
        openness = 0.9f + 0.1f * u;
    } else if (at < GLASS_SIGH_IN_MS + GLASS_SIGH_TOP_MS + GLASS_SIGH_OUT_MS) {
        float u = settle((float)(at - GLASS_SIGH_IN_MS - GLASS_SIGH_TOP_MS) / (float)GLASS_SIGH_OUT_MS);
        radius = lerpf(R_SIGH, R_REST, u);
        openness = 1.f - u;
    } else {
        radius = R_REST;
        openness = 0.12f;
    }

    glass_look_clear(out);
    out->radius = radius;
    out->radius_second = radius_second;
    out->second = second;
    out->amp = 0.88f + 0.22f * openness;
    out->mote = 0.12f + 0.88f * openness;
    out->light_angle = (float)elapsed_ms * 0.00018f;
    if (elapsed_ms >= 260u) {
        uint32_t back = elapsed_ms - 260u;
        uint32_t cycle = back % GLASS_SIGH_CYCLE_MS;
        float lag = R_REST;
        if (cycle < GLASS_SIGH_IN_MS) {
            lag = lerpf(R_REST, 188.f, smooth((float)cycle / (float)GLASS_SIGH_IN_MS));
        } else if (cycle < GLASS_SIGH_IN_MS + GLASS_SIGH_TOP_MS) {
            float u = smooth((float)(cycle - GLASS_SIGH_IN_MS) / (float)GLASS_SIGH_TOP_MS);
            lag = lerpf(188.f, R_SIGH, u);
        } else if (cycle < GLASS_SIGH_IN_MS + GLASS_SIGH_TOP_MS + GLASS_SIGH_OUT_MS) {
            float u = settle((float)(cycle - GLASS_SIGH_IN_MS - GLASS_SIGH_TOP_MS) / (float)GLASS_SIGH_OUT_MS);
            lag = lerpf(R_SIGH, R_REST, u);
        }
        out->radius_lag = lag;
    } else {
        out->radius_lag = radius;
    }
}
