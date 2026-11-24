#include "glass.h"

#include <math.h>
#include <string.h>

#define CX 232.5f
#define CY 232.5f
#define N_MOTES 40

typedef struct {
    float ang;
    float rad;
    float speed;
    float size;
    float tw;
} mote;

static mote motes[N_MOTES];
static float noise_tab[256];
static float gauss_lut[640];
static int ready;

static float gauss_at(float d, float sigma)
{
    float x = fabsf(d) / sigma;
    int i = (int)(x * 64.f);
    if (i >= 640) {
        return 0.f;
    }
    return gauss_lut[i];
}

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

static float smooth(float edge0, float edge1, float x)
{
    float t = clampf((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static float ang_noise(float x)
{
    float i;
    float f;
    int i0;
    int i1;

    x = fmodf(x, 256.f);
    if (x < 0.f) {
        x += 256.f;
    }
    i = floorf(x);
    f = x - i;
    f = f * f * (3.f - 2.f * f);
    i0 = (int)i & 255;
    i1 = (i0 + 1) & 255;
    return noise_tab[i0] * (1.f - f) + noise_tab[i1] * f;
}

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

void glass_render_init(void)
{
    uint32_t seed = 0x51A7u;
    int i;

    if (ready) {
        return;
    }
    for (i = 0; i < 640; i++) {
        float x = (float)i / 64.f;
        gauss_lut[i] = expf(-0.5f * x * x);
    }
    for (i = 0; i < 256; i++) {
        noise_tab[i] = (float)(lcg(&seed) & 1023) / 1023.f * 2.f - 1.f;
    }
    for (i = 0; i < 8; i++) {
        float copy[256];
        int k;
        memcpy(copy, noise_tab, sizeof(copy));
        for (k = 0; k < 256; k++) {
            noise_tab[k] = (copy[(k + 255) & 255] + copy[k] * 2.f + copy[(k + 1) & 255]) * 0.25f;
        }
    }
    for (i = 0; i < N_MOTES; i++) {
        uint32_t r = lcg(&seed);
        motes[i].ang = (float)(r & 1023) / 1023.f * 6.2831853f;
        motes[i].rad = 0.78f + (float)((r >> 10) & 255) / 255.f * 0.42f;
        motes[i].speed = 0.00015f + (float)((r >> 18) & 255) / 255.f * 0.00045f;
        if (i & 1) {
            motes[i].speed = -motes[i].speed;
        }
        motes[i].size = 1.3f + (float)((r >> 4) & 15) / 15.f * 1.6f;
        motes[i].tw = 0.4f + (float)((r >> 8) & 15) / 15.f;
    }
    ready = 1;
}

static void add_heat(float *px, float heat, float warmth)
{
    float w;
    float r;
    float g;
    float b;

    if (heat <= 0.f) {
        return;
    }
    w = clampf(warmth, 0.f, 1.f);
    r = heat * (0.90f + 0.10f * w);
    g = heat * (0.34f + 0.22f * heat + 0.20f * w);
    b = heat * heat * (0.07f + 0.16f * w);
    px[0] += r;
    px[1] += g;
    px[2] += b;
}

static void splat(float *acc, float x, float y, float sigma, float heat, float warmth, float gain)
{
    int x0 = (int)floorf(x - sigma * 3.f);
    int y0 = (int)floorf(y - sigma * 3.f);
    int x1 = (int)ceilf(x + sigma * 3.f);
    int y1 = (int)ceilf(y + sigma * 3.f);
    int yy;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= GLASS_W) {
        x1 = GLASS_W - 1;
    }
    if (y1 >= GLASS_H) {
        y1 = GLASS_H - 1;
    }
    for (yy = y0; yy <= y1; yy++) {
        int xx;
        for (xx = x0; xx <= x1; xx++) {
            float dx = (float)xx - x;
            float dy = (float)yy - y;
            float d2 = dx * dx + dy * dy;
            float pr = hypotf((float)xx - CX, (float)yy - CY);
            float mask = 1.f - smooth(228.f, 233.f, pr);
            float g = expf(-0.5f * d2 / (sigma * sigma));
            add_heat(acc + (yy * GLASS_W + xx) * 3, g * heat * gain * mask, warmth);
        }
    }
}

void glass_render(float *acc, const glass_look *look, uint32_t ms, float dt)
{
    const float tau = 0.11f;
    float keep = expf(-dt / tau);
    float gain = 1.f - keep;
    float warmth = clampf(look->warmth, 0.f, 1.f);
    int i;
    int n = GLASS_W * GLASS_H;

    glass_render_init();
    for (i = 0; i < n * 3; i++) {
        acc[i] *= keep;
    }

    for (i = 0; i < n; i++) {
        int x = i % GLASS_W;
        int y = i / GLASS_W;
        float dx = (float)x - (CX + look->sway);
        float dy = (float)y - CY;
        float pr = hypotf(dx, dy);
        float panel = hypotf((float)x - CX, (float)y - CY);
        float mask = 1.f - smooth(228.f, 233.f, panel);
        float ang = atan2f(dx, -dy);
        float n1 = ang_noise(ang * 40.7f + (float)ms * 0.004f);
        float n2 = ang_noise(ang * 90.f + 40.f - (float)ms * 0.0025f);
        float crescent = 0.74f + 0.26f * cosf(ang - look->light_angle);
        float gather = clampf(look->gather, 0.f, 1.f);
        float wobble = (1.f - gather) * (1.6f * n1 + 0.7f * n2);
        float radius = look->radius + wobble;
        float d = pr - radius;
        float bloom = gauss_at(d, 42.f);
        float body = gauss_at(d, 14.f);
        float lip = gauss_at(d - 0.4f, 2.6f);
        float shade = gauss_at(d + 9.f, 4.2f);
        float dl = pr - look->radius_lag;
        float lag = gauss_at(dl, 22.f);
        float noise_mul = lerpf(0.90f + 0.10f * n1, 1.f, gather);
        float heat;
        float core;
        float rim;
        float *px;

        heat = (0.16f * bloom + 0.50f * body + 0.22f * lip - 0.10f * shade) * look->amp;
        heat += lag * 0.14f * look->amp;
        heat *= crescent * noise_mul;
        if (look->second > 0.01f) {
            float d2 = pr - look->radius_second;
            heat += gauss_at(d2, 3.2f) * 0.42f * look->second;
        }
        core = gauss_at(pr, 28.f);
        heat += core * (0.035f + 0.055f * look->amp);
        rim = gauss_at(panel - 226.f, 1.15f);
        heat += rim * 0.028f;

        if (look->disk > 0.004f) {
            float ac = ang < 0.f ? ang + 6.2831853f : ang;
            float eaten = (1.f - clampf(look->disk, 0.f, 1.f)) * 6.2831853f;
            float lit = smooth(eaten - 0.35f, eaten + 0.95f, ac);
            float moon_r = 168.f;
            float moon = 1.f - smooth(moon_r - 18.f, moon_r + 22.f, pr);
            float limb = gauss_at(ac - eaten, 0.32f);
            heat += moon * lit * 0.34f;
            heat += limb * moon * 0.10f * look->disk;
        }

        if (look->arc > 0.01f) {
            float ac = ang < 0.f ? ang + 6.2831853f : ang;
            float mid = 0.f;
            float wrap = ac > 3.1415926f ? ac - 6.2831853f : ac;
            float band = gauss_at(wrap, 0.55f);
            float line = gauss_at(panel - 214.f, 2.2f);
            (void)mid;
            heat += band * line * 0.10f * look->arc;
        }

        if (look->marks > 0.5f) {
            int m;
            for (m = 0; m < 5; m++) {
                float a = -1.15f + (float)m * 0.55f;
                float mx = CX + cosf(a) * 198.f;
                float my = CY + sinf(a) * 198.f;
                float md = hypotf((float)x - mx, (float)y - my);
                float on = (float)m < look->marks_lit ? 1.f : 0.28f;
                heat += expf(-0.5f * (md * md) / (3.2f * 3.2f)) * 0.16f * on;
            }
        }

        px = acc + i * 3;
        add_heat(px, fmaxf(0.f, heat) * gain * mask, warmth);
    }

    for (i = 0; i < N_MOTES; i++) {
        float a = motes[i].ang + (float)ms * motes[i].speed;
        float rr = look->radius * motes[i].rad;
        float tw = 0.65f + 0.35f * sinf((float)ms * 0.003f * motes[i].tw + motes[i].ang);
        float x = CX + look->sway + cosf(a) * rr;
        float y = CY + sinf(a) * rr;
        float heat = 0.20f * look->mote * tw * (1.f - 0.65f * look->gather);
        splat(acc, x, y, motes[i].size, heat, warmth, gain);
    }

    if (look->bead > 0.02f) {
        float x = CX + look->sway + cosf(look->bead_angle) * (look->radius * 1.02f);
        float y = CY + sinf(look->bead_angle) * (look->radius * 1.02f);
        splat(acc, x, y, 2.4f, 0.55f * look->bead, warmth, gain);
    }
}

static float knee(float x)
{
    if (x < 0.82f) {
        return x;
    }
    {
        float u = (x - 0.82f) / 0.7f;
        return 0.82f + 0.18f * (u / (1.f + u));
    }
}

static const uint8_t BAYER[16] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5
};

void glass_quantize(const float *acc, uint16_t *rgb565, uint8_t *rgb888)
{
    int i;
    for (i = 0; i < GLASS_W * GLASS_H; i++) {
        int x = i % GLASS_W;
        int y = i / GLASS_W;
        float dither = ((float)BAYER[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.f - 0.5f;
        float r = knee(acc[i * 3 + 0]);
        float g = knee(acc[i * 3 + 1]);
        float b = knee(acc[i * 3 + 2]);
        int r5;
        int g6;
        int b5;

        r = clampf(r + dither * (1.f / 31.f), 0.f, 1.f);
        g = clampf(g + dither * (1.f / 63.f), 0.f, 1.f);
        b = clampf(b + dither * (1.f / 31.f), 0.f, 1.f);
        r5 = (int)(r * 31.f + 0.5f);
        g6 = (int)(g * 63.f + 0.5f);
        b5 = (int)(b * 31.f + 0.5f);
        if (r5 > 31) {
            r5 = 31;
        }
        if (g6 > 63) {
            g6 = 63;
        }
        if (b5 > 31) {
            b5 = 31;
        }
        if (rgb565) {
            rgb565[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
        if (rgb888) {
            rgb888[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            rgb888[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            rgb888[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
    }
}
