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

static void run_paint_tests(void);
static void run_hand_tests(void);

static void run_body_tests(void)
{
    glass_body body;
    glass_sense sense;
    glass_imu imu;
    glass_touch touch;
    glass_ear ear;
    sigrun s;
    int i;
    int saw_sway;
    float t;

    glass_body_init(&body);
    memset(&sense, 0, sizeof(sense));
    memset(&imu, 0, sizeof(imu));
    imu.az = 1.f;
    for (i = 0; i < 80; i++) {
        glass_body_imu(&body, &imu, 20, &sense);
    }
    expect(sense.still > 0.9f, "a still hand gathers");
    expect(!sense.walking, "a still hand is not walking");
    expect(!sense.swaying, "a still hand is not swaying");
    expect(sense.fidget < 0.15f, "a still hand has no bead");

    glass_body_init(&body);
    saw_sway = 0;
    for (i = 0; i < 400; i++) {
        t = (float)i * 0.02f;
        memset(&imu, 0, sizeof(imu));
        imu.ax = 0.30f * sinf(6.2831853f * t / 2.5f);
        imu.az = 1.f;
        glass_body_imu(&body, &imu, 20, &sense);
        if (i > 200 && imu.ax > 0.25f && sense.swaying && sense.sway > 0.4f && !sense.walking) {
            saw_sway = 1;
        }
    }
    expect(saw_sway, "a rock is sway");

    glass_body_init(&body);
    for (i = 0; i < 200; i++) {
        t = (float)i * 0.02f;
        memset(&imu, 0, sizeof(imu));
        imu.az = 1.f + 0.45f * sinf(6.2831853f * t / 0.5f);
        glass_body_imu(&body, &imu, 20, &sense);
    }
    expect(sense.walking, "steps are walking");
    expect(!sense.swaying, "steps are not sway");

    glass_body_init(&body);
    for (i = 0; i < 50; i++) {
        memset(&imu, 0, sizeof(imu));
        imu.az = 1.f;
        imu.gx = 40.f;
        glass_body_imu(&body, &imu, 20, &sense);
    }
    expect(!sense.walking, "a fidget is not a step");
    expect(sense.fidget > 0.4f, "restless motion draws the bead");
    expect(sense.still < 0.2f, "a fidget does not gather");

    glass_body_init(&body);
    memset(&touch, 0, sizeof(touch));
    touch.down = 1;
    for (i = 0; i < 80; i++) {
        t = (float)i * 0.05f;
        touch.x = 233.f + 140.f * cosf(6.2831853f * t / 10.f);
        touch.y = 233.f + 140.f * sinf(6.2831853f * t / 10.f);
        glass_body_touch(&body, &touch, 50, &sense);
    }
    expect(sense.finger_down, "a traced circle is a finger");
    expect(sense.finger_period > 8.f && sense.finger_period < 12.f, "a traced circle has its period");

    glass_body_init(&body);
    for (i = 0; i < 40; i++) {
        t = (float)i * 0.05f;
        touch.x = 233.f + 140.f * cosf(6.2831853f * t / 2.f);
        touch.y = 233.f + 140.f * sinf(6.2831853f * t / 2.f);
        glass_body_touch(&body, &touch, 50, &sense);
    }
    expect(sense.finger_period > 1.5f && sense.finger_period < 2.6f, "a fast trace is still a trace");
    sigrun_init(&s);
    sense.now_ms = 1000;
    sigrun_step(&s, &sense, 50);
    expect(s.source == SIGRUN_SRC_FINGER, "a fast trace leads");
    expect(s.breath_period >= GLASS_MIN_PERIOD_S - 0.01f, "a fast trace cannot pant");

    glass_body_init(&body);
    touch.x = 233.f;
    touch.y = 80.f;
    touch.down = 1;
    for (i = 0; i < 30; i++) {
        glass_body_touch(&body, &touch, 50, &sense);
    }
    expect(sense.finger_down, "a held finger is down");
    expect(sense.finger_period < 0.05f, "a held finger has no period");

    glass_body_init(&body);
    memset(&ear, 0, sizeof(ear));
    ear.rms = 0.01f;
    ear.low = 0.004f;
    for (i = 0; i < 20; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    expect(sense.room_quiet, "a quiet room stays quiet");
    expect(sense.near_breath < 0.2f, "a quiet room has no breath");

    ear.rms = 0.02f;
    ear.low = 0.01f;
    for (i = 0; i < 10; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    ear.rms = 0.12f;
    ear.low = 0.03f;
    for (i = 0; i < 12; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    expect(sense.room_quiet, "a close breath is in a quiet room");
    expect(sense.near_breath > 0.25f, "a close breath is heard");

    glass_body_init(&body);
    ear.rms = 0.02f;
    ear.low = 0.01f;
    for (i = 0; i < 8; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    ear.rms = 0.10f;
    ear.low = 0.09f;
    for (i = 0; i < 12; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    expect(sense.hum > 0.5f, "a low breath is warm");

    glass_body_init(&body);
    ear.rms = 0.80f;
    ear.low = 0.20f;
    for (i = 0; i < 10; i++) {
        glass_body_ear(&body, &ear, 50, &sense);
    }
    expect(!sense.room_quiet, "a loud room stays on the clock");
    expect(sense.near_breath < 0.05f, "a loud room is not a breath");
    expect(sense.hum < 0.05f, "a loud room is not warm");
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
    expect(s.scene == SIGRUN_TIDE && s.tick == 1 && s.noise == 0 && s.task_set_ms == 0u, "a press selects the tick off the charger");
    expect(s.tick_ms == 0u && s.look.mote > 0.8f, "the tick starts on the beat");
    in.rescue = 0;
    in.now_ms += 16;
    sigrun_step(&s, &in, 200);
    expect(s.tick_ms == 200u && s.look.mote < 0.3f, "the mote rests between ticks");
    sigrun_step(&s, &in, 800);
    expect(s.tick_ms == 0u && s.look.mote > 0.8f, "the tick returns after one second");
    in.rescue = 1;
    in.now_ms += 16;
    sigrun_step(&s, &in, 16);
    expect(s.task_set_ms == 300000u && s.catalog == 0, "another press selects the short moon");

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
    expect(s.scene == SIGRUN_TIDE, "sighs return to the breath");

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

    run_body_tests();
    run_paint_tests();
    run_hand_tests();

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

static void expand565(const uint16_t *px, uint8_t *rgb)
{
    int i;

    for (i = 0; i < GLASS_W * GLASS_H; i++) {
        int r5 = (px[i] >> 11) & 31;
        int g6 = (px[i] >> 5) & 63;
        int b5 = px[i] & 31;
        rgb[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        rgb[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        rgb[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}

static void run_hand_tests(void)
{
    glass_body body;
    glass_touch touch;
    glass_sense sense;
    sigrun s;
    glass_sense in;
    int i;
    float far;

    glass_body_init(&body);
    memset(&touch, 0, sizeof(touch));
    memset(&sense, 0, sizeof(sense));
    body.have_bead = 1;
    body.bx = 190.f;
    body.bvx = 900.f;
    for (i = 0; i < 40; i++) {
        glass_body_touch(&body, &touch, 20, &sense);
        far = sqrtf(body.bx * body.bx + body.by * body.by);
        expect(far < 203.f, "the bead stays inside");
    }
    expect(body.bvx < 0.f, "the bead bounces back");

    glass_body_init(&body);
    touch.down = 1;
    touch.x = 233.f;
    touch.y = 197.f;
    glass_body_touch(&body, &touch, 20, &sense);
    expect(sense.bead_grab, "a finger on the bead grabs it");
    touch.x = 310.f;
    glass_body_touch(&body, &touch, 20, &sense);
    touch.down = 0;
    glass_body_touch(&body, &touch, 20, &sense);
    expect(body.bvx > 80.f, "a flick keeps its speed");

    glass_body_init(&body);
    memset(&touch, 0, sizeof(touch));
    body.have_bead = 1;
    body.bx = 90.f;
    for (i = 0; i < 50; i++) {
        glass_body_touch(&body, &touch, 20, &sense);
    }
    expect(body.bx > 70.f, "the bead waits a second");
    for (i = 0; i < 90; i++) {
        glass_body_touch(&body, &touch, 20, &sense);
    }
    expect(fabsf(body.bx) < 12.f && fabsf(body.by) < 12.f, "the bead returns to the center");

    sigrun_init(&s);
    in = sense_clear();
    in.charging = 1;
    in.now_ms = 1000;
    in.tap = 1;
    in.tap_x = 320.f;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    sigrun_step(&s, &in, 20);
    expect(s.card == 1 && s.tick == 1 && s.noise == 0 && s.look.mote > 0.8f, "a press selects the tick");
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.task_set_ms == 300000u && s.look.disk > 0.3f && s.look.disk < 0.5f, "the next press selects the short moon");
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.task_set_ms == 600000u && s.look.disk > 0.55f && s.look.disk < 0.8f, "the next press selects the ten minute moon");
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.task_set_ms == 1500000u && s.look.disk > 0.9f, "the next press fills the moon");
    expect(s.scene == SIGRUN_TIDE, "a press on the charger does not sigh");
    in.charging = 0;
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.catalog == 0 && s.task_running == 1 && s.task_left_ms == 1500000u, "unplug starts the moon");
    expect(s.look.disk > 0.9f, "the running moon starts full");
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.noise == 1 && s.look.arc > 0.8f, "the next press selects white noise");
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.noise == 2 && s.look.warmth > 0.3f, "the next press selects pink");
    in.rescue = 0;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    in.rescue = 1;
    in.now_ms += 20;
    sigrun_step(&s, &in, 20);
    expect(s.noise == 3 && s.look.gather > 0.6f, "the next press selects brown");

    sigrun_init(&s);
    in = sense_clear();
    s.card = 4;
    s.task_set_ms = 1000u;
    s.task_armed = 1;
    sigrun_step(&s, &in, 20);
    sigrun_step(&s, &in, 1000);
    expect(s.task_running == 0 && s.task_end_ms == 1, "time up starts the ending");
    sigrun_step(&s, &in, 700);
    expect(s.look.disk > 0.5f, "the moon comes back when time is up");
    sigrun_step(&s, &in, 800);
    expect(s.look.disk > 0.9f && s.look.lamp > 0.9f, "the returned moon holds at full brightness");
    sigrun_step(&s, &in, 2000);
    expect(s.card == 0 && s.noise == 0 && s.task_set_ms == 0u && s.look.disk < 0.02f, "the light fades back to a plain breath");

    sigrun_init(&s);
    in = sense_clear();
    in.charging = 1;
    s.card = 1;
    in.rescue = 1;
    sigrun_step(&s, &in, 20);
    expect(s.card == 2 && s.numeral == 5 && s.look.numeral == 0, "five waits a second");
    in.rescue = 0;
    sigrun_step(&s, &in, 1000);
    expect(s.look.numeral == 5 && s.look.numeral_light > 0.9f, "five is shown");
    sigrun_step(&s, &in, 2200);
    expect(s.look.numeral == 5 && s.look.numeral_light < 0.8f && s.look.numeral_light > 0.3f, "five starts to fade");
    sigrun_step(&s, &in, 400);
    expect(s.numeral == 0 && s.look.numeral == 0, "five is gone");
    in.rescue = 1;
    sigrun_step(&s, &in, 20);
    in.rescue = 0;
    sigrun_step(&s, &in, 1000);
    expect(s.look.numeral == 10, "ten is shown");
    in.rescue = 1;
    sigrun_step(&s, &in, 20);
    in.rescue = 0;
    sigrun_step(&s, &in, 1000);
    expect(s.look.numeral == 25, "twenty-five is shown");
}

static void run_paint_tests(void)
{
    uint16_t *px = calloc((size_t)GLASS_W * GLASS_H, sizeof(uint16_t));
    uint8_t *heat = calloc((size_t)GLASS_W * GLASS_H, 1);
    glass_look look;
    int y;
    int lit = 0;

    if (!px || !heat) {
        fprintf(stderr, "fail paint buffers\n");
        g_failed = 1;
        free(px);
        free(heat);
        return;
    }
    glass_look_tide(4500, &look);
    glass_paint(px, heat, &look);
    expect(px[0] == 0, "a corner stays off");
    y = 233 - (int)look.radius;
    if (y > 2 && y < GLASS_H - 2) {
        int x;
        for (x = 220; x <= 246; x++) {
            if (px[y * GLASS_W + x] != 0) {
                lit = 1;
            }
        }
    }
    expect(lit, "the ring is lit");

    glass_look_clear(&look);
    look.marks = 1.f;
    look.marks_lit = 3.f;
    look.disk = 0.5f;
    look.radius = 120.f;
    look.arc = 0.8f;
    look.bead = 0.9f;
    glass_paint(px, heat, &look);
    expect(px[233 * GLASS_W + 180] != 0, "a half moon is drawn");
    glass_look_clear(&look);
    look.numeral = 5;
    look.numeral_light = 1.f;
    look.radius = 120.f;
    glass_paint(px, heat, &look);
    expect(px[145 * GLASS_W + 233] != 0, "a five is drawn");
    free(px);
    free(heat);
}

static void render_at(uint32_t ms, uint16_t *px, uint8_t *heat, uint8_t *rgb)
{
    glass_look look;

    direct(ms, &look);
    ease_look(&look);
    glass_paint(px, heat, &look);
    expand565(px, rgb);
}

int main(int argc, char **argv)
{
    uint16_t *px;
    uint8_t *heat;
    uint8_t *rgb;

    if (argc < 2) {
        fprintf(stderr, "usage: preview test | still <ms> <file.ppm> | reel\n");
        return 2;
    }
    if (strcmp(argv[1], "test") == 0) {
        run_tests();
        return 0;
    }

    px = calloc((size_t)GLASS_W * GLASS_H, sizeof(uint16_t));
    heat = calloc((size_t)GLASS_W * GLASS_H, 1);
    rgb = calloc((size_t)GLASS_W * GLASS_H * 3, 1);
    if (!px || !heat || !rgb) {
        return 1;
    }

    if (strcmp(argv[1], "still") == 0) {
        uint32_t ms = (uint32_t)strtoul(argv[2], NULL, 10);
        uint32_t t;
        for (t = ms > 400 ? ms - 400 : 0; t <= ms; t += 33) {
            render_at(t, px, heat, rgb);
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
            render_at(t, px, heat, rgb);
            if (fwrite(rgb, 1, (size_t)GLASS_W * GLASS_H * 3, stdout) != (size_t)GLASS_W * GLASS_H * 3) {
                return 1;
            }
        }
        return 0;
    }

    fprintf(stderr, "unknown command\n");
    return 2;
}
