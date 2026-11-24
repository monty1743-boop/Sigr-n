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

static int wrap_i(int v, int n)
{
    if (v < 0) {
        return n - 1;
    }
    if (v >= n) {
        return 0;
    }
    return v;
}

static void show_mode(sigrun *s)
{
    s->look.arc = 0.f;
    s->look.disk = 0.f;
    if (s->card == 1) {
        s->look.gather = 0.f;
        s->look.warmth = 0.f;
        s->look.mote = s->tick_ms < 120u ? 1.f : 0.12f;
    } else if (s->card == 2) {
        s->look.disk = 0.34f;
    } else if (s->card == 3) {
        s->look.disk = 0.66f;
    } else if (s->card == 4) {
        s->look.disk = 1.f;
    } else if (s->card == 5) {
        s->look.mote = 0.95f;
        s->look.gather = 0.f;
        s->look.warmth = 0.f;
        s->look.arc = 0.95f;
    } else if (s->card == 6) {
        s->look.mote = 0.5f;
        s->look.warmth = 0.45f;
        s->look.arc = 0.55f;
    } else if (s->card == 7) {
        s->look.mote = 0.12f;
        s->look.gather = 0.72f;
        s->look.warmth = 0.9f;
        s->look.arc = 0.22f;
    }
}

static void choose_mode(sigrun *s)
{
    static const int kinds[] = {0, 0, 0, 0, 0, 1, 2, 3};
    static const uint32_t tasks[] = {0u, 0u, 300000u, 600000u, 1500000u, 0u, 0u, 0u};

    s->card = wrap_i(s->card + 1, 8);
    s->noise = kinds[s->card];
    s->task_set_ms = tasks[s->card];
    s->task_running = 0;
    s->task_left_ms = 0;
    s->task_armed = s->task_set_ms > 0u ? 1 : 0;
    s->task_end_ms = 0;
    s->numeral = 0;
    s->numeral_ms = 0;
    if (s->card == 2) {
        s->numeral = 5;
        s->numeral_ms = 1;
    } else if (s->card == 3) {
        s->numeral = 10;
        s->numeral_ms = 1;
    } else if (s->card == 4) {
        s->numeral = 25;
        s->numeral_ms = 1;
    }
}

static void settle_breath(sigrun *s)
{
    s->card = 0;
    s->noise = 0;
    s->task_set_ms = 0;
    s->task_running = 0;
    s->task_left_ms = 0;
    s->task_armed = 0;
    s->task_end_ms = 0;
    s->numeral = 0;
    s->numeral_ms = 0;
    s->tick = 0;
    s->tick_ms = 0;
    s->look.disk = 0.f;
    s->look.arc = 0.f;
    s->look.lamp = 0.f;
    s->look.numeral = 0;
    s->look.numeral_light = 0.f;
}

static void apply_modifiers(sigrun *s, const glass_sense *in, uint32_t dt_ms)
{
    float glide = (float)dt_ms / (45.f + (float)dt_ms);
    float target_x = clampf(in->rock_x, -1.f, 1.f) * 42.f;
    float target_y = clampf(in->rock_y, -1.f, 1.f) * 42.f;

    s->look.gather = clampf(in->still, 0.f, 1.f);
    s->look.warmth = clampf(in->hum, 0.f, 1.f);
    s->look.arc = 0.f;
    s->look.bead = 0.62f + 0.38f * clampf(in->fidget, 0.f, 1.f);
    s->look.bead_angle = (float)s->phase_ms * 0.004f;
    if (in->bead_x > 1.f || in->bead_y > 1.f) {
        s->look.bead_free = 1;
        s->look.bead_x = in->bead_x;
        s->look.bead_y = in->bead_y;
    }
    s->sway_px += glide * (target_x - s->sway_px);
    s->sway_py += glide * (target_y - s->sway_py);
    s->look.sway = s->sway_px;
    s->look.sway_y = s->sway_py;
    show_mode(s);
    s->look.lamp = 0.f;
    s->look.numeral = 0;
    s->look.numeral_light = 0.f;
    if (s->numeral > 0 && s->numeral_ms >= 1000u && s->numeral_ms < 3400u) {
        float light = 1.f;
        if (s->numeral_ms > 3000u) {
            light = 1.f - (float)(s->numeral_ms - 3000u) / 400.f;
            if (light < 0.f) {
                light = 0.f;
            }
        }
        s->look.numeral = s->numeral;
        s->look.numeral_light = light;
    }
    if (s->task_set_ms > 0u) {
        float icon = s->card == 2 ? 0.34f : (s->card == 3 ? 0.66f : 1.f);
        if (s->task_end_ms > 0) {
            float t = (float)s->task_end_ms;
            float u;
            float lamp;
            if (t < 1000.f) {
                u = t / 1000.f;
                lamp = u;
            } else if (t < 1600.f) {
                u = 1.f;
                lamp = 1.f;
            } else {
                float fade = (t - 1600.f) / 1600.f;
                if (fade > 1.f) {
                    fade = 1.f;
                }
                u = 1.f - fade;
                lamp = 1.f - fade;
            }
            s->look.disk = icon * u;
            s->look.lamp = lamp;
        } else if (s->task_running) {
            s->look.disk = clampf((float)s->task_left_ms / (float)s->task_set_ms, 0.f, 1.f) * icon;
        } else if (!s->task_armed) {
            s->look.disk = 0.f;
        }
    }
}

static void drive_breath(sigrun *s, const glass_sense *in, uint32_t dt_ms)
{
    int hold;
    uint32_t next;
    uint32_t prev_period = (uint32_t)(s->breath_period * 1000.f);

    if (!in->charging && !in->bead_grab && in->finger_period > 0.05f) {
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
        if (prev_period > 80u && (period_ms + 80u < prev_period || period_ms > prev_period + 80u) && s->phase_ms > 0u) {
            s->phase_ms = (uint32_t)((uint64_t)s->phase_ms * period_ms / prev_period);
            if (s->phase_ms >= period_ms) {
                s->phase_ms = period_ms - 1u;
            }
        }
        exhale_at = (uint32_t)((uint64_t)period_ms * GLASS_TIDE_IN_MS / GLASS_TIDE_MS);
        hold = !in->charging && !in->bead_grab && in->finger_down && !in->walking && s->source != SIGRUN_SRC_WALK;
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
            s->look.radius = 118.f + in->near_breath * 88.f;
            s->look.mote = 0.2f + 0.7f * in->near_breath;
        } else {
            uint32_t shaped = (uint32_t)((uint64_t)s->phase_ms * GLASS_TIDE_MS / period_ms);
            glass_look_tide(shaped, &s->look);
        }
    }
}

void sigrun_step(sigrun *s, const glass_sense *in, uint32_t dt_ms)
{
    int ending = s->task_end_ms > 0;
    int numbering = s->numeral_ms > 0;
    int was_tick = s->tick;

    s->catalog = in->charging ? 1 : 0;
    if (in->rescue) {
        choose_mode(s);
        ending = 0;
        numbering = 0;
    }
    s->tick = s->card == 1 ? 1 : 0;
    if (!s->tick || !was_tick) {
        s->tick_ms = 0;
    } else {
        s->tick_ms += dt_ms;
        while (s->tick_ms >= 1000u) {
            s->tick_ms -= 1000u;
        }
    }
    if (!in->charging && s->task_armed && !s->task_running && s->task_end_ms == 0 && s->scene == SIGRUN_TIDE) {
        s->task_running = 1;
        s->task_left_ms = s->task_set_ms;
        s->task_armed = 0;
    } else if (s->task_running && s->scene == SIGRUN_TIDE && !in->charging) {
        if (dt_ms >= s->task_left_ms) {
            s->task_left_ms = 0;
            s->task_running = 0;
            s->task_end_ms = 1;
        } else {
            s->task_left_ms -= dt_ms;
        }
    }
    if (numbering && s->numeral_ms > 0) {
        s->numeral_ms += dt_ms;
        if (s->numeral_ms >= 3400u) {
            s->numeral_ms = 0;
            s->numeral = 0;
        }
    }
    if (ending && s->task_end_ms > 0) {
        s->task_end_ms += dt_ms;
        if (s->task_end_ms >= 3200u) {
            settle_breath(s);
        }
    }

    s->scene_elapsed += dt_ms;

    if (s->scene == SIGRUN_SIGH && s->scene_elapsed >= GLASS_SIGH_TOTAL_MS) {
        s->scene = SIGRUN_TIDE;
        s->scene_elapsed = 0;
    } else if (s->scene == SIGRUN_GROUND && s->scene_elapsed >= GLASS_GROUND_MS) {
        s->scene = SIGRUN_TIDE;
        s->scene_elapsed = 0;
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

    apply_modifiers(s, in, dt_ms);
}
