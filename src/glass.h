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
    float sway_y;
    float gather;
    float bead;
    float bead_angle;
    int bead_free;
    float bead_x;
    float bead_y;
    float light_angle;
    float arc;
    float marks;
    float marks_lit;
    int numeral;
    float numeral_light;
    float lamp;
    float rune;
    float cell;
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
    float rock_x;
    float rock_y;
    float grav_x;
    float grav_y;
    float finger_period;
    int finger_down;
    int room_quiet;
    float near_breath;
    float hum;
    float fidget;
    float still;
    int charging;
    float cell;
    int list_left;
    int has_task;
    uint32_t task_ms;
    int tap;
    int hold;
    float tap_x;
    int bead_grab;
    float bead_x;
    float bead_y;
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
    int card;
    int list_set;
    uint32_t task_set_ms;
    int word;
    int noise;
    int task_armed;
    uint32_t task_end_ms;
    int tick;
    uint32_t tick_ms;
    uint32_t boot_ms;
    int numeral;
    uint32_t numeral_ms;
    float sway_px;
    float sway_py;
    float sway_vx;
    float sway_vy;
} sigrun;

void sigrun_init(sigrun *s);
void sigrun_step(sigrun *s, const glass_sense *in, uint32_t dt_ms);

void glass_look_ease(glass_look *drawn, const glass_look *goal, uint32_t dt_ms);
void glass_paint(uint16_t *rgb565, uint8_t *heat, const glass_look *look);

typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
} glass_imu;

typedef struct {
    int down;
    float x, y;
} glass_touch;

typedef struct {
    float rms;
    float low;
} glass_ear;

typedef struct {
    float grav[3];
    float lean[3];
    int primed;
    int axis;
    int rising;
    float axis_s;
    float peak;
    uint32_t since_peak;
    uint32_t gaps[3];
    int gap_n;
    uint32_t quiet_ms;
    uint32_t up_ms;
    float fidget;
    float sway_amp;
    float sway_lp;
    float tilt_x;
    float tilt_y;
    int sway_on;
    int down_was;
    int hold_sent;
    uint32_t down_ms;
    float down_x;
    float down_y;
    float last_x;
    float last_y;
    float bx;
    float by;
    float bvx;
    float bvy;
    int grabbed;
    int on_bead;
    int have_bead;
    uint32_t alone_ms;
    float ang;
    int have_ang;
    float omega;
    float noise;
    int noise_primed;
    float env;
} glass_body;

void glass_body_init(glass_body *b);
void glass_body_imu(glass_body *b, const glass_imu *imu, uint32_t dt_ms, glass_sense *out);
void glass_body_touch(glass_body *b, const glass_touch *touch, uint32_t dt_ms, glass_sense *out);
void glass_body_ear(glass_body *b, const glass_ear *ear, uint32_t dt_ms, glass_sense *out);
