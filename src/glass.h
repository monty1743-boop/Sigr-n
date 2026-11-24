#pragma once

#include <stdint.h>

#define GLASS_W 466
#define GLASS_H 466

#define GLASS_TIDE_MS 10000u
#define GLASS_TIDE_IN_MS 4500u
#define GLASS_SIGH_IN_MS 1400u
#define GLASS_SIGH_TOP_MS 700u
#define GLASS_SIGH_OUT_MS 4300u
#define GLASS_SIGH_REST_MS 800u
#define GLASS_SIGH_CYCLE_MS (GLASS_SIGH_IN_MS + GLASS_SIGH_TOP_MS + GLASS_SIGH_OUT_MS + GLASS_SIGH_REST_MS)
#define GLASS_SIGH_CYCLES 3
#define GLASS_SIGH_TOTAL_MS (GLASS_SIGH_CYCLE_MS * GLASS_SIGH_CYCLES)
#define GLASS_MIN_PERIOD_S 7.5f
#define GLASS_GROUND_MS 20000u
#define GLASS_RESCUE_WINDOW_MS 60000u

typedef struct {
    float radius;
    float radius_lag;
    float radius_second;
    float second;
    float amp;
    float mote;
    float warmth;
    float disk;
    float sway;
    float gather;
    float bead;
    float bead_angle;
    float light_angle;
    float arc;
    float marks;
    float marks_lit;
} glass_look;

void glass_look_clear(glass_look *out);
void glass_look_tide(uint32_t ms, glass_look *out);
void glass_look_sigh(uint32_t elapsed_ms, glass_look *out);
void glass_look_walk(uint32_t ms, glass_look *out);

typedef enum {
    SIGRUN_TIDE = 0,
    SIGRUN_SIGH,
    SIGRUN_GROUND,
    SIGRUN_LIST
} sigrun_scene;

typedef enum {
    SIGRUN_SRC_CLOCK = 0,
    SIGRUN_SRC_FINGER,
    SIGRUN_SRC_WALK,
    SIGRUN_SRC_SWAY,
    SIGRUN_SRC_BREATH
} sigrun_source;

typedef struct {
    uint32_t now_ms;
    int rescue;
    int walking;
    int swaying;
    float sway;
    float finger_period;
    int finger_down;
    int room_quiet;
    float near_breath;
    float hum;
    float fidget;
    float still;
    int charging;
    int list_left;
    int has_task;
    uint32_t task_ms;
} glass_sense;

typedef struct {
    sigrun_scene scene;
    uint32_t scene_elapsed;
    uint32_t rescue_window_start;
    int rescue_count;
    int grounded;
    uint32_t phase_ms;
    float breath_period;
    sigrun_source source;
    int catalog;
    int task_running;
    uint32_t task_left_ms;
    glass_look look;
} sigrun;

void sigrun_init(sigrun *s);
void sigrun_step(sigrun *s, const glass_sense *in, uint32_t dt_ms);

void glass_render_init(void);
void glass_render(float *acc, const glass_look *look, uint32_t ms, float dt);
void glass_quantize(const float *acc, uint16_t *rgb565, uint8_t *rgb888);
