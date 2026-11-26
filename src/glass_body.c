#include "glass.h"

#include <math.h>
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

static void screen_grav(float ax, float ay, float *sx, float *sy)
{
    /* This panel's tilt demo moves X with accel Y and Y with accel X. */
    *sx = ay;
    *sy = ax;
}

static float rock_axis(float g)
{
    float a = fabsf(g);

    if (a <= 0.02f) {
        return 0.f;
    }
    a = (a - 0.02f) / 0.15f;
    if (a > 1.f) {
        a = 1.f;
    }
    return copysignf(a, g);
}

static float mag3(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

static int dominant(float x, float y, float z)
{
    float ax = fabsf(x);
    float ay = fabsf(y);
    float az = fabsf(z);

    if (ax >= ay && ax >= az) {
        return 0;
    }
    if (ay >= az) {
        return 1;
    }
    return 2;
}

void glass_body_init(glass_body *b)
{
    memset(b, 0, sizeof(*b));
    b->axis = -1;
    b->since_peak = 5000u;
}

void glass_body_imu(glass_body *b, const glass_imu *imu, uint32_t dt_ms, glass_sense *out)
{
    float a[3];
    float d[3];
    float dt;
    float dyn;
    float gyro;
    float lat;
    float still;
    float target;
    int axis;
    int i;
    int peaked;
    int walking;

    if (dt_ms < 1u) {
        dt_ms = 1u;
    }
    if (dt_ms > 100u) {
        dt_ms = 100u;
    }
    dt = (float)dt_ms / 1000.f;
    a[0] = imu->ax;
    a[1] = imu->ay;
    a[2] = imu->az;

    if (!b->primed) {
        memcpy(b->grav, a, sizeof(a));
        memcpy(b->lean, a, sizeof(a));
        b->primed = 1;
    } else {
        float ag = dt / (0.40f + dt);
        float al = dt / (1.80f + dt);

        for (i = 0; i < 3; i++) {
            b->grav[i] += ag * (a[i] - b->grav[i]);
            b->lean[i] += al * (b->grav[i] - b->lean[i]);
        }
    }

    for (i = 0; i < 3; i++) {
        d[i] = a[i] - b->grav[i];
    }
    dyn = mag3(d[0], d[1], d[2]);
    gyro = mag3(imu->gx, imu->gy, imu->gz);
    axis = dominant(d[0], d[1], d[2]);
    if (b->axis >= 0 && fabsf(d[axis]) < fabsf(d[b->axis]) * 2.f) {
        axis = b->axis;
    }
    if (axis != b->axis) {
        b->axis = axis;
        b->rising = 0;
        b->peak = 0;
        b->axis_s = d[axis];
    }

    peaked = 0;
    if (d[axis] > b->axis_s + 0.002f) {
        b->rising = 1;
        if (d[axis] > b->peak) {
            b->peak = d[axis];
        }
    } else if (b->rising && b->axis_s > 0.16f) {
        peaked = 1;
    }
    b->axis_s = d[axis];

    if (peaked) {
        if (b->since_peak >= 280u && b->since_peak <= 900u) {
            if (b->gap_n < 3) {
                b->gaps[b->gap_n++] = b->since_peak;
            } else {
                b->gaps[0] = b->gaps[1];
                b->gaps[1] = b->gaps[2];
                b->gaps[2] = b->since_peak;
            }
        } else {
            b->gap_n = 0;
        }
        b->since_peak = 0;
        b->rising = 0;
        b->peak = 0;
    } else {
        b->since_peak += dt_ms;
        if (b->since_peak > 1200u) {
            b->gap_n = 0;
        }
    }

    walking = b->gap_n >= 3 && b->since_peak < 1000u;
    lat = b->grav[0] - b->lean[0];
    if (fabsf(b->grav[1] - b->lean[1]) > fabsf(lat)) {
        lat = b->grav[1] - b->lean[1];
    }
    b->sway_amp += (dt / (0.70f + dt)) * (fabsf(lat) - b->sway_amp);
    if (b->up_ms < 1500u) {
        b->sway_amp = 0.f;
    }
    b->sway_lp += (dt / (0.22f + dt)) * (clampf(lat / 0.18f, -1.f, 1.f) - b->sway_lp);
    b->tilt_x += (dt / (0.14f + dt)) * (imu->ax - b->tilt_x);
    b->tilt_y += (dt / (0.14f + dt)) * (imu->ay - b->tilt_y);

    b->up_ms += dt_ms;
    if (dyn < 0.045f && gyro < 8.f) {
        b->quiet_ms += dt_ms;
    } else {
        b->quiet_ms = 0;
    }
    still = b->quiet_ms > 700u ? 1.f : (float)b->quiet_ms / 700.f;
    target = gyro / 60.f;
    if (!walking && dyn > 0.05f && dyn < 0.20f) {
        target += dyn / 0.20f;
    }
    target = clampf(target, 0.f, 1.f);
    b->fidget += (dt / (0.25f + dt)) * (target - b->fidget);
    if (b->fidget > 0.45f || walking) {
        still = 0.f;
    }

    if (walking || b->up_ms < 1500u) {
        b->sway_on = 0;
    } else if (b->sway_on) {
        b->sway_on = b->sway_amp > 0.035f;
    } else {
        b->sway_on = b->sway_amp > 0.09f && dyn < 0.40f;
    }

    out->walking = walking;
    out->swaying = b->sway_on;
    out->sway = b->sway_lp;
    {
        float sx;
        float sy;

        screen_grav(b->tilt_x, b->tilt_y, &sx, &sy);
        out->rock_x = rock_axis(sx);
        out->rock_y = rock_axis(sy);
        out->grav_x = sx;
        out->grav_y = sy;
    }
    out->still = clampf(still, 0.f, 1.f);
    out->fidget = clampf(b->fidget, 0.f, 1.f);
}

static float upright_of(float gx, float gy)
{
    float m = sqrtf(gx * gx + gy * gy);
    float t = (m - 0.55f) / (0.92f - 0.55f);

    if (t < 0.f) {
        t = 0.f;
    }
    if (t > 1.f) {
        t = 1.f;
    }
    return t * t * (3.f - 2.f * t);
}

static void bead_keep_in(glass_body *b, float upright)
{
    int n;
    float give = upright > 0.5f ? 1.22f : 1.45f;

    for (n = 0; n < 2; n++) {
        float d = sqrtf(b->bx * b->bx + b->by * b->by);
        float lim = 202.f;
        float nx;
        float ny;
        float vn;

        if (d <= lim || d < 0.5f) {
            break;
        }
        nx = b->bx / d;
        ny = b->by / d;
        vn = b->bvx * nx + b->bvy * ny;
        if (vn > 0.f) {
            b->bvx -= give * vn * nx;
            b->bvy -= give * vn * ny;
        }
        if (upright > 0.5f) {
            b->bvx *= 0.82f;
            b->bvy *= 0.82f;
        }
        b->bx = nx * lim;
        b->by = ny * lim;
    }
}

static void bead_step(glass_body *b, const glass_touch *touch, uint32_t dt_ms, glass_sense *out)
{
    float dt = (float)dt_ms / 1000.f;
    float sx;
    float sy;
    float dx;
    float dy;
    int near;

    if (dt < 0.001f) {
        dt = 0.001f;
    }
    if (dt > 0.08f) {
        dt = 0.08f;
    }
    if (!b->have_bead) {
        b->bx = 0.f;
        b->by = -36.f;
        b->have_bead = 1;
    }
    sx = 233.f + b->bx;
    sy = 233.f + b->by;
    dx = touch->x - sx;
    dy = touch->y - sy;
    near = dx * dx + dy * dy < 52.f * 52.f;
    if (touch->down && !b->down_was && near) {
        b->grabbed = 1;
        b->on_bead = 1;
    }
    if (b->grabbed && touch->down) {
        float nx = touch->x - 233.f;
        float ny = touch->y - 233.f;
        float d = sqrtf(nx * nx + ny * ny);
        float follow = dt / (0.045f + dt);
        float ivx;
        float ivy;
        float sp;

        b->alone_ms = 0;
        if (d > 202.f) {
            nx *= 202.f / d;
            ny *= 202.f / d;
        }
        ivx = (nx - b->bx) / dt;
        ivy = (ny - b->by) / dt;
        b->bvx += (ivx - b->bvx) * follow;
        b->bvy += (ivy - b->bvy) * follow;
        sp = sqrtf(b->bvx * b->bvx + b->bvy * b->bvy);
        if (sp > 720.f) {
            b->bvx *= 720.f / sp;
            b->bvy *= 720.f / sp;
        }
        b->bx += (nx - b->bx) * follow;
        b->by += (ny - b->by) * follow;
        out->bead_grab = 1;
    } else {
        float pull = 0.f;
        float gx;
        float gy;
        float upright;

        screen_grav(b->tilt_x, b->tilt_y, &gx, &gy);
        upright = upright_of(gx, gy);

        b->grabbed = 0;
        if (b->alone_ms < 1000u) {
            b->alone_ms += dt_ms;
        } else {
            pull = 1.f;
        }
        b->bvx += gx * 2200.f * upright * dt;
        b->bvy += gy * 2200.f * upright * dt;
        if (pull > 0.f) {
            float home_k = 7.5f * (1.f - upright);
            b->bvx += (-home_k * b->bx - 4.8f * b->bvx) * dt;
            b->bvy += (-home_k * b->by - 4.8f * b->bvy) * dt;
        }
        b->bx += b->bvx * dt;
        b->by += b->bvy * dt;
        b->bvx *= expf(-0.55f * dt);
        b->bvy *= expf(-0.55f * dt);
        bead_keep_in(b, upright);
        if (b->alone_ms >= 1000u && upright < 0.2f) {
            float home = b->bx * b->bx + b->by * b->by;
            float speed = b->bvx * b->bvx + b->bvy * b->bvy;
            if (home < 16.f && speed < 80.f) {
                b->bx = 0.f;
                b->by = 0.f;
                b->bvx = 0.f;
                b->bvy = 0.f;
            }
        }
    }
    out->bead_x = 233.f + b->bx;
    out->bead_y = 233.f + b->by;
}

void glass_body_touch(glass_body *b, const glass_touch *touch, uint32_t dt_ms, glass_sense *out)
{
    float moved;
    int dial;

    out->tap = 0;
    out->hold = 0;
    out->tap_x = 0.f;
    out->finger_down = 0;
    out->bead_grab = 0;
    out->finger_x = touch->x;
    out->finger_y = touch->y;
    out->press_ms = 0;
    bead_step(b, touch, dt_ms, out);
    if (!b->down_was && touch->down) {
        b->down_ms = 0;
        b->down_x = touch->x;
        b->down_y = touch->y;
        b->hold_sent = 0;
        b->have_ang = 0;
    }
    if (touch->down) {
        b->down_ms += dt_ms;
        if (b->down_ms > 50u) {
            out->finger_down = 1;
        }
        if (!b->on_bead && !b->hold_sent && b->down_ms >= 480u) {
            b->hold_sent = 1;
            out->hold = 1;
            out->tap_x = b->down_x;
        }
    } else if (b->down_was) {
        moved = (b->last_x - b->down_x) * (b->last_x - b->down_x) + (b->last_y - b->down_y) * (b->last_y - b->down_y);
        if (!b->on_bead && !b->hold_sent && b->down_ms >= 40u && b->down_ms <= 400u && moved < 36.f * 36.f) {
            out->tap = 1;
            out->tap_x = b->down_x;
        }
        b->on_bead = 0;
        b->omega *= 0.5f;
        b->have_ang = 0;
        b->down_ms = 0;
    } else {
        b->omega *= 0.85f;
    }
    dial = touch->down && b->timer_face && b->down_ms >= GLASS_DIAL_MS;
    if (dial) {
        b->grabbed = 0;
        b->on_bead = 0;
        out->bead_grab = 0;
        b->have_ang = 0;
        b->omega = 0.f;
        b->bvx = 0.f;
        b->bvy = 0.f;
    }
    out->press_ms = touch->down ? b->down_ms : 0;

    if (touch->down && dt_ms > 0u && !b->grabbed && !dial) {
        float dx = touch->x - (float)GLASS_W * 0.5f;
        float dy = touch->y - (float)GLASS_H * 0.5f;
        float r = sqrtf(dx * dx + dy * dy);

        if (r > 48.f) {
            float ang = atan2f(dy, dx);

            if (b->have_ang && b->down_was) {
                float delta = ang - b->ang;
                float px = touch->x - b->last_x;
                float py = touch->y - b->last_y;
                float chord = sqrtf(px * px + py * py);
                float inst;

                if (delta > 3.14159265f) {
                    delta -= 6.2831853f;
                }
                if (delta < -3.14159265f) {
                    delta += 6.2831853f;
                }
                inst = delta / ((float)dt_ms / 1000.f);
                if (chord < 80.f && fabsf(inst) < 8.f) {
                    float blend = (float)dt_ms / (450.f + (float)dt_ms);
                    b->omega += blend * (inst - b->omega);
                }
            }
            b->ang = ang;
            b->have_ang = 1;
        }
        b->last_x = touch->x;
        b->last_y = touch->y;
    }
    b->down_was = touch->down ? 1 : 0;

    if (b->grabbed || fabsf(b->omega) <= 0.28f) {
        out->finger_period = 0.f;
    } else {
        out->finger_period = 6.2831853f / fabsf(b->omega);
    }
}

void glass_body_ear(glass_body *b, const glass_ear *ear, uint32_t dt_ms, glass_sense *out)
{
    float dt = (float)dt_ms / 1000.f;
    float excess;
    float breath;
    float ratio;
    int quiet;

    if (dt < 0.001f) {
        dt = 0.001f;
    }
    if (!b->noise_primed) {
        b->noise = ear->rms;
        b->env = ear->rms;
        b->noise_primed = 1;
    } else {
        float ae = dt / (0.12f + dt);

        if (ear->rms < b->noise) {
            b->noise = ear->rms;
        } else {
            float an = dt / (4.f + dt);
            b->noise += an * (ear->rms - b->noise);
        }
        b->env += ae * (ear->rms - b->env);
    }

    excess = b->env - b->noise;
    if (excess < 0.f) {
        excess = 0.f;
    }
    breath = clampf(excess / 0.08f, 0.f, 1.f);
    quiet = b->noise < 0.055f && ear->rms < 0.28f;
    if (!quiet) {
        breath = 0.f;
    }
    ratio = ear->low / (ear->rms > 0.008f ? ear->rms : 0.008f);
    ratio = clampf(ratio, 0.f, 1.f);

    out->room_quiet = quiet;
    out->near_breath = breath;
    out->hum = quiet ? ratio * breath : 0.f;
}
