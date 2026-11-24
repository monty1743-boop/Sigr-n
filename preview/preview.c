#include "glass.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        g_failed = 1;
    }
}

static glass_sense sense_clear(void)
{
    glass_sense in;
    memset(&in, 0, sizeof(in));
    in.task_ms = 600000u;
    return in;
}

static void run_tests(void)
{
    sigrun s;
    glass_sense in;
    int i;

    sigrun_init(&s);
    in = sense_clear();
    for (i = 0; i < 50; i++) {
        in.now_ms = (uint32_t)(i * 100);
        sigrun_step(&s, &in, 100);
    }
    expect(s.scene == SIGRUN_TIDE, "idle stays tide");
    expect(s.source == SIGRUN_SRC_CLOCK, "idle clock");
    expect(s.catalog == 0, "unplugged hides catalog");

    in = sense_clear();
    in.charging = 1;
    in.now_ms = 5000;
    sigrun_step(&s, &in, 100);
    expect(s.catalog == 1, "charger opens one catalog");
    in.charging = 0;
    in.now_ms = 5100;
    sigrun_step(&s, &in, 100);
    expect(s.catalog == 0, "unplug closes catalog");

    in = sense_clear();
    in.rescue = 1;
    in.now_ms = 8000;
    sigrun_step(&s, &in, 16);
    expect(s.scene == SIGRUN_SIGH, "rescue is the sigh");
    in.rescue = 0;
    for (i = 0; i < 40; i++) {
        in.now_ms += 16;
        in.walking = 1;
        sigrun_step(&s, &in, 16);
    }
    expect(s.scene == SIGRUN_SIGH, "walking does not cancel a sigh");

    in.walking = 0;
    in.rescue = 1;
    in.now_ms += 16;
    sigrun_step(&s, &in, 16);
    expect(s.scene == SIGRUN_GROUND, "second rescue in a minute grounds");

    sigrun_init(&s);
    in = sense_clear();
    in.list_left = 3;
    in.rescue = 1;
    in.now_ms = 1000;
    sigrun_step(&s, &in, 16);
    in.rescue = 0;
    while (s.scene == SIGRUN_SIGH) {
        in.now_ms += 200;
        sigrun_step(&s, &in, 200);
    }
    expect(s.scene == SIGRUN_LIST, "a list appears after the sighs");

    sigrun_init(&s);
    in = sense_clear();
    in.finger_period = 2.f;
    in.now_ms = 100;
    sigrun_step(&s, &in, 50);
    expect(s.source == SIGRUN_SRC_FINGER, "a traced circle leads");
    expect(s.breath_period >= GLASS_MIN_PERIOD_S - 0.01f, "trace cannot pant");

    sigrun_init(&s);
    in = sense_clear();
    s.phase_ms = 9000;
    s.breath_period = 10.f;
    in.finger_down = 1;
    in.now_ms = 100;
    sigrun_step(&s, &in, 2000);
    expect(s.phase_ms == 9999u, "a held finger waits at the bottom of the exhale");
    in.finger_down = 0;
    in.now_ms = 300;
    sigrun_step(&s, &in, 50);
    expect(s.phase_ms < 100u, "releasing the finger starts the next inhale");

    sigrun_init(&s);
    in = sense_clear();
    in.walking = 1;
    in.swaying = 1;
    in.finger_period = 0.f;
    sigrun_step(&s, &in, 30);
    expect(s.source == SIGRUN_SRC_WALK, "steps outrank sway");

    if (g_failed) {
        fprintf(stderr, "logic tests failed\n");
        exit(1);
    }
    printf("logic tests passed\n");
}

static void direct(uint32_t ms, glass_look *out)
{
    const uint32_t tide = 6000u;
    const uint32_t sigh = tide + GLASS_SIGH_TOTAL_MS;
    const uint32_t back = sigh + 3000u;
    const uint32_t moon = back + 8000u;
    const uint32_t walk = moon + 7000u;
    const uint32_t sway = walk + 6000u;
    const uint32_t still = sway + 5000u;
    const uint32_t hum = still + 6000u;

    if (ms < tide) {
        glass_look_tide(ms, out);
    } else if (ms < sigh) {
        glass_look_sigh(ms - tide, out);
    } else if (ms < back) {
        glass_look_tide(ms, out);
    } else if (ms < moon) {
        float u = (float)(ms - back) / 8000.f;
        glass_look_tide(ms, out);
        out->disk = 1.f - u;
        out->bead = 0.85f;
        out->bead_angle = (float)ms * 0.003f;
    } else if (ms < walk) {
        glass_look_walk(ms, out);
    } else if (ms < sway) {
        glass_look_tide(ms, out);
        out->sway = sinf((float)(ms - walk) * 0.0012f) * 36.f;
        out->light_angle += out->sway * 0.05f;
    } else if (ms < still) {
        glass_look_tide(ms, out);
        out->gather = 0.92f;
        out->mote *= 0.25f;
    } else if (ms < hum) {
        float p = (float)(ms % GLASS_TIDE_MS) / (float)GLASS_TIDE_MS;
        glass_look_tide(ms, out);
        out->warmth = p > 0.45f ? 0.85f : 0.15f;
    } else {
        glass_look_tide(ms, out);
    }
}

static uint32_t reel_ms(void)
{
    return 6000u + GLASS_SIGH_TOTAL_MS + 3000u + 8000u + 7000u + 6000u + 5000u + 6000u;
}

static int write_ppm(const char *path, const uint8_t *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", GLASS_W, GLASS_H);
    if (fwrite(rgb, 1, (size_t)GLASS_W * GLASS_H * 3, f) != (size_t)GLASS_W * GLASS_H * 3) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void ease_look(glass_look *look)
{
    static glass_look held;
    static int have;
    float d;

    if (have) {
        d = look->radius - held.radius;
        if (fabsf(d) > 3.4f) {
            look->radius = held.radius + copysignf(3.4f, d);
        }
        d = look->radius_lag - held.radius_lag;
        if (fabsf(d) > 3.4f) {
            look->radius_lag = held.radius_lag + copysignf(3.4f, d);
        }
        d = look->second - held.second;
        if (fabsf(d) > 0.08f) {
            look->second = held.second + copysignf(0.08f, d);
        }
        d = look->disk - held.disk;
        if (fabsf(d) > 0.025f) {
            look->disk = held.disk + copysignf(0.025f, d);
        }
        d = look->sway - held.sway;
        if (fabsf(d) > 2.8f) {
            look->sway = held.sway + copysignf(2.8f, d);
        }
    }
    held = *look;
    have = 1;
}

static void render_at(uint32_t ms, float *acc, uint8_t *rgb, int clear)
{
    glass_look look;
    if (clear) {
        memset(acc, 0, (size_t)GLASS_W * GLASS_H * 3 * sizeof(float));
    }
    direct(ms, &look);
    ease_look(&look);
    glass_render(acc, &look, ms, 1.f / 30.f);
    glass_quantize(acc, NULL, rgb);
}

int main(int argc, char **argv)
{
    float *acc;
    uint8_t *rgb;

    if (argc < 2) {
        fprintf(stderr, "usage: preview test | still <ms> <file.ppm> | reel\n");
        return 2;
    }
    if (strcmp(argv[1], "test") == 0) {
        run_tests();
        return 0;
    }

    acc = calloc((size_t)GLASS_W * GLASS_H * 3, sizeof(float));
    rgb = calloc((size_t)GLASS_W * GLASS_H * 3, 1);
    if (!acc || !rgb) {
        return 1;
    }
    glass_render_init();

    if (strcmp(argv[1], "still") == 0) {
        uint32_t ms = (uint32_t)strtoul(argv[2], NULL, 10);
        uint32_t t;
        for (t = ms > 400 ? ms - 400 : 0; t <= ms; t += 33) {
            render_at(t, acc, rgb, 0);
        }
        if (write_ppm(argv[3], rgb) != 0) {
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "reel") == 0) {
        uint32_t end = reel_ms();
        uint32_t t;
        for (t = 0; t < end; t += 33) {
            render_at(t, acc, rgb, 0);
            if (fwrite(rgb, 1, (size_t)GLASS_W * GLASS_H * 3, stdout) != (size_t)GLASS_W * GLASS_H * 3) {
                return 1;
            }
        }
        return 0;
    }

    fprintf(stderr, "unknown command\n");
    return 2;
}
