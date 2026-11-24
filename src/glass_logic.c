#include "glass.h"

#include <string.h>

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

void sigrun_init(sigrun *s)
{
    memset(s, 0, sizeof(*s));
    s->scene = SIGRUN_TIDE;
    s->breath_period = 10.f;
    s->source = SIGRUN_SRC_CLOCK;
    glass_look_tide(0, &s->look);
}

static void apply_modifiers(sigrun *s, const glass_sense *in)
{
    s->look.gather = clampf(in->still, 0.f, 1.f);
    s->look.warmth = clampf(in->hum, 0.f, 1.f);
    s->look.arc = 0.f;
    s->look.bead = clampf(in->fidget, 0.f, 1.f);
    s->look.bead_angle = (float)s->phase_ms * 0.004f;
    if (in->swaying && s->source == SIGRUN_SRC_SWAY) {
        s->look.sway = clampf(in->sway, -1.f, 1.f) * 12.f;
        s->look.light_angle += in->sway * 0.8f;
    }
    if (in->has_task && s->task_running && s->scene == SIGRUN_TIDE) {
        float total = in->task_ms ? (float)in->task_ms : 600000.f;
        s->look.disk = clampf((float)s->task_left_ms / total, 0.f, 1.f);
    }
    if (s->scene == SIGRUN_LIST) {
        s->look.marks = 1.f;
        s->look.marks_lit = (float)in->list_left;
    }
}

static void drive_breath(sigrun *s, const glass_sense *in, uint32_t dt_ms)
{
    int hold;
    uint32_t next;

    if (in->finger_period > 0.05f) {
        s->source = SIGRUN_SRC_FINGER;
        s->breath_period = in->finger_period < GLASS_MIN_PERIOD_S ? GLASS_MIN_PERIOD_S : in->finger_period;
    } else if (in->walking) {
        s->source = SIGRUN_SRC_WALK;
        s->breath_period = 5.6f;
    } else if (in->swaying) {
        s->source = SIGRUN_SRC_SWAY;
        s->breath_period = 10.f;
    } else if (in->room_quiet && in->near_breath > 0.25f) {
        s->source = SIGRUN_SRC_BREATH;
        s->breath_period = 10.f;
    } else {
        s->source = SIGRUN_SRC_CLOCK;
        s->breath_period = 10.f;
    }

    {
        uint32_t period_ms = (uint32_t)(s->breath_period * 1000.f);
        uint32_t exhale_at;

        if (period_ms < 1u) {
            period_ms = 1u;
        }
        exhale_at = (uint32_t)((uint64_t)period_ms * GLASS_TIDE_IN_MS / GLASS_TIDE_MS);
        hold = in->finger_down && !in->walking && s->source != SIGRUN_SRC_WALK;
        next = s->phase_ms + dt_ms;
        if (hold && s->phase_ms >= exhale_at && next >= period_ms) {
            s->phase_ms = period_ms - 1u;
        } else {
            s->phase_ms = next % period_ms;
        }

        if (s->source == SIGRUN_SRC_WALK) {
            glass_look_walk(s->phase_ms, &s->look);
        } else if (s->source == SIGRUN_SRC_BREATH) {
            glass_look_tide(s->phase_ms, &s->look);
            s->look.radius = 96.f + in->near_breath * 62.f;
            s->look.mote = 0.2f + 0.7f * in->near_breath;
        } else {
            uint32_t shaped = (uint32_t)((uint64_t)s->phase_ms * GLASS_TIDE_MS / period_ms);
            glass_look_tide(shaped, &s->look);
        }
    }
}

void sigrun_step(sigrun *s, const glass_sense *in, uint32_t dt_ms)
{
    s->catalog = in->charging ? 1 : 0;

    if (in->rescue) {
        int in_window = s->rescue_count > 0 && (in->now_ms - s->rescue_window_start) < GLASS_RESCUE_WINDOW_MS;
        if (in_window && !s->grounded) {
            s->scene = SIGRUN_GROUND;
            s->scene_elapsed = 0;
            s->grounded = 1;
            s->rescue_count += 1;
        } else {
            s->scene = SIGRUN_SIGH;
            s->scene_elapsed = 0;
            s->grounded = 0;
            s->rescue_window_start = in->now_ms;
            s->rescue_count = 1;
        }
    } else {
        s->scene_elapsed += dt_ms;
    }

    if (s->scene == SIGRUN_SIGH && s->scene_elapsed >= GLASS_SIGH_TOTAL_MS) {
        s->scene = in->list_left > 0 ? SIGRUN_LIST : SIGRUN_TIDE;
        s->scene_elapsed = 0;
    } else if (s->scene == SIGRUN_GROUND && s->scene_elapsed >= GLASS_GROUND_MS) {
        s->scene = SIGRUN_TIDE;
        s->scene_elapsed = 0;
    } else if (s->scene == SIGRUN_LIST && in->list_left <= 0) {
        s->scene = SIGRUN_TIDE;
        s->scene_elapsed = 0;
    }

    if (in->has_task && !in->charging && !s->task_running && s->scene == SIGRUN_TIDE && s->task_left_ms == 0) {
        s->task_running = 1;
        s->task_left_ms = in->task_ms ? in->task_ms : 600000u;
    }
    if (s->task_running && s->scene == SIGRUN_TIDE) {
        if (dt_ms >= s->task_left_ms) {
            s->task_left_ms = 0;
            s->task_running = 0;
        } else {
            s->task_left_ms -= dt_ms;
        }
    }

    if (s->scene == SIGRUN_SIGH) {
        glass_look_sigh(s->scene_elapsed, &s->look);
        s->source = SIGRUN_SRC_CLOCK;
    } else if (s->scene == SIGRUN_GROUND) {
        float pulse = (float)(s->scene_elapsed % 4000u) / 4000.f;
        glass_look_tide(s->phase_ms, &s->look);
        s->look.amp *= 0.75f + 0.25f * (pulse < 0.5f ? pulse * 2.f : (1.f - pulse) * 2.f);
        s->look.mote *= 0.4f;
    } else {
        drive_breath(s, in, dt_ms);
    }

    apply_modifiers(s, in);
}
