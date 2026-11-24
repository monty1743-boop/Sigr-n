#include "glass.h"

#include <math.h>
#include <string.h>

#define STEPS 128
#define CX 233
#define CY 233

static int16_t s_cos[STEPS];
static int16_t s_sin[STEPS];
static int s_ready;

static const uint8_t BAYER[16] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5
};

static int clampi(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void paint_init(void)
{
    int i;

    if (s_ready) {
        return;
    }
    for (i = 0; i < STEPS; i++) {
        float a = (float)i * 6.2831853f / (float)STEPS;
        s_cos[i] = (int16_t)(cosf(a) * 1024.f);
        s_sin[i] = (int16_t)(sinf(a) * 1024.f);
    }
    s_ready = 1;
}

static int ang_index(float rad)
{
    int i;

    rad = rad * (float)STEPS / 6.2831853f;
    i = (int)rad;
    if (rad < 0.f && (float)i != rad) {
        i -= 1;
    }
    return i & (STEPS - 1);
}

static void polar(int cx, int cy, int index, int radius, int *x, int *y)
{
    index &= STEPS - 1;
    *x = cx + (s_cos[index] * radius) / 1024;
    *y = cy + (s_sin[index] * radius) / 1024;
}

static int isqrt(int n)
{
    int x = 0;
    int bit = 1 << 14;

    if (n <= 0) {
        return 0;
    }
    while (bit > n) {
        bit >>= 2;
    }
    while (bit) {
        int t = x + bit;
        x >>= 1;
        if (t <= n) {
            n -= t;
            x += bit;
        }
        bit >>= 2;
    }
    return x;
}

static void put(uint8_t *heat, int x, int y, int h)
{
    int pdx;
    int pdy;
    uint8_t *p;

    if ((unsigned)x >= GLASS_W || (unsigned)y >= GLASS_H || h <= 0) {
        return;
    }
    pdx = x - CX;
    pdy = y - CY;
    if (pdx * pdx + pdy * pdy > 232 * 232) {
        return;
    }
    p = heat + y * GLASS_W + x;
    if (h > 255) {
        h = 255;
    }
    if (h > *p) {
        *p = (uint8_t)h;
    }
}

static void stamp(uint8_t *heat, int x0, int y0, int rad, int heat0)
{
    int dy;
    int r2;

    if (heat0 <= 0) {
        return;
    }
    if (rad < 1) {
        rad = 1;
    }
    heat0 = clampi(heat0, 0, 255);
    r2 = rad * rad;
    for (dy = -rad; dy <= rad; dy++) {
        int dx;
        for (dx = -rad; dx <= rad; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2) {
                continue;
            }
            put(heat, x0 + dx, y0 + dy, heat0 * (r2 - d2) / r2);
        }
    }
}

static int thicken(int radius, int glow)
{
    float t = ((float)radius - 168.f) / (206.f - 168.f);

    if (t < 0.f) {
        t = 0.f;
    } else if (t > 1.f) {
        t = 1.f;
    }
    t = t * t * (3.f - 2.f * t);
    return glow + (int)((float)glow * t + 0.5f);
}

static void ring(uint8_t *heat, int cx, int cy, int radius, int glow, int heat0)
{
    int y;
    int inner;
    int outer;
    int rad2;
    int norm;
    int y0;
    int y1;
    int i;
    int g2;
    uint8_t fall[64];

    if (radius < 8 || heat0 <= 0) {
        return;
    }
    if (glow < 2) {
        glow = 2;
    }
    heat0 = clampi(heat0, 0, 255);
    if (glow > 63) {
        glow = 63;
    }
    inner = radius - glow;
    if (inner < 0) {
        inner = 0;
    }
    outer = radius + glow;
    rad2 = radius * radius;
    norm = (1 << 16) / (radius * 2);
    g2 = glow * glow;
    for (i = 0; i <= glow; i++) {
        fall[i] = (uint8_t)(heat0 * (g2 - i * i) / g2);
    }
    y0 = cy - outer;
    y1 = cy + outer;
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= GLASS_H) {
        y1 = GLASS_H - 1;
    }
    for (y = y0; y <= y1; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        int out2 = outer * outer - dy2;
        int xout;
        int xin;
        int x;
        uint8_t *row;

        if (out2 < 0) {
            continue;
        }
        xout = isqrt(out2);
        if (dy2 >= inner * inner) {
            xin = 0;
        } else {
            int hole = inner * inner - dy2;
            xin = isqrt(hole);
            if (xin * xin < hole) {
                xin++;
            }
        }
        row = heat + (size_t)y * GLASS_W;
        for (x = xin; x <= xout; x++) {
            int delta = x * x + dy2 - rad2;
            int h;
            int side;

            if (delta < 0) {
                delta = -delta;
            }
            h = (delta * norm) >> 16;
            if (h > glow) {
                continue;
            }
            h = fall[h];
            if (h <= 0) {
                continue;
            }
            for (side = 0; side < 2; side++) {
                int px = side == 0 ? cx + x : cx - x;
                int pdx;
                int pdy;

                if (side == 1 && x == 0) {
                    break;
                }
                if ((unsigned)px >= GLASS_W) {
                    continue;
                }
                pdx = px - CX;
                pdy = y - CY;
                if (pdx * pdx + pdy * pdy > 232 * 232) {
                    continue;
                }
                if (h > row[px]) {
                    row[px] = (uint8_t)h;
                }
            }
        }
    }
}

static void moon(uint8_t *heat, int cx, int cy, float disk)
{
    int r = 150;
    int edge;
    int y;
    const int soft = 36;

    if (disk < 0.02f) {
        return;
    }
    edge = cx + (int)((disk * 2.f - 1.f) * (float)r);
    for (y = -r; y <= r; y++) {
        int xs = isqrt(r * r - y * y);
        int x;
        for (x = -xs; x <= xs; x++) {
            int d = edge - (cx + x);
            int h;
            if (d < -soft) {
                continue;
            }
            h = d >= soft ? 66 : (66 * (d + soft)) / (soft * 2);
            put(heat, cx + x, cy + y, h);
        }
    }
}

static void stroke(uint8_t *heat, int x0, int y0, int x1, int y1, int rad, int h)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int span = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    int n = span / 8;
    int i;

    if (n < 1) {
        n = 1;
    }
    for (i = 0; i <= n; i++) {
        stamp(heat, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, rad, h);
    }
}

static void digit(uint8_t *heat, int cx, int cy, int d, int h)
{
    int x0 = cx - 48;
    int x1 = cx + 48;
    int y0 = cy - 88;
    int y1 = cy;
    int y2 = cy + 88;

    if (d == 1) {
        stroke(heat, cx, y0, cx, y2, 16, h);
        return;
    }
    if (d != 4) {
        stroke(heat, x0, y0, x1, y0, 16, h);
    }
    if (d == 0 || d == 2 || d == 3) {
        stroke(heat, x1, y0, x1, y1, 16, h);
    }
    if (d == 0 || d == 5 || d == 6) {
        stroke(heat, x0, y0, x0, y1, 16, h);
    }
    if (d != 0 && d != 1 && d != 7) {
        stroke(heat, x0, y1, x1, y1, 16, h);
    }
    if (d == 0 || d == 3 || d == 5 || d == 6) {
        stroke(heat, x1, y1, x1, y2, 16, h);
    }
    if (d == 0 || d == 2 || d == 6) {
        stroke(heat, x0, y1, x0, y2, 16, h);
    }
    if (d != 4 && d != 7) {
        stroke(heat, x0, y2, x1, y2, 16, h);
    }
}

static void numerals(uint8_t *heat, int cx, int cy, int n, float light)
{
    int h = (int)(light * 220.f);

    if (h < 8 || n <= 0) {
        return;
    }
    if (n >= 10) {
        digit(heat, cx - 78, cy, n / 10, h);
        digit(heat, cx + 78, cy, n % 10, h);
    } else {
        digit(heat, cx, cy, n, h);
    }
}

static void marks(uint8_t *heat, int cx, int cy, float lit)
{
    int m;

    for (m = 0; m < 5; m++) {
        int x;
        int y;
        int h = (float)m < lit ? 210 : 48;
        polar(cx, cy, ang_index(-1.15f + (float)m * 0.55f), 226, &x, &y);
        stamp(heat, x, y, 5, h);
    }
}

static void quantize(uint16_t *rgb, const uint8_t *heat, float warmth)
{
    int w = clampi((int)(warmth * 255.f), 0, 255);
    int rm = 24 + w * 186 / 255;
    int gm = 224 - w * 74 / 255;
    int bm = 198 - w * 148 / 255;
    uint16_t lut[256];
    int h;
    int i;

    for (h = 0; h < 256; h++) {
        int r = h * rm / 255;
        int g = h * gm / 255;
        int b = h * bm / 255;
        lut[h] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    i = 0;
    for (int y = 0; y < GLASS_H; y++) {
        int row = (y & 3) * 4;
        for (int x = 0; x < GLASS_W; x++, i++) {
            int sample;
            if (heat[i] == 0) {
                continue;
            }
            {
                int spread = heat[i] < 48 ? 3 : 1;
                sample = heat[i] + ((int)BAYER[row + (x & 3)] - 8) * spread;
            }
            rgb[i] = lut[clampi(sample, 0, 255)];
        }
    }
}

static float ease_toward(float cur, float goal, float dt, float tau)
{
    float a = dt / (tau + dt);
    return cur + (goal - cur) * a;
}

void glass_look_ease(glass_look *drawn, const glass_look *goal, uint32_t dt_ms)
{
    float dt = (float)dt_ms / 1000.f;

    if (drawn->radius < 1.f) {
        *drawn = *goal;
        return;
    }
    if (dt < 0.016f) {
        dt = 0.016f;
    }
    if (dt > 0.12f) {
        dt = 0.12f;
    }
    drawn->radius = ease_toward(drawn->radius, goal->radius, dt, 0.10f);
    drawn->radius_lag = ease_toward(drawn->radius_lag, goal->radius_lag, dt, 0.14f);
    drawn->radius_second = ease_toward(drawn->radius_second, goal->radius_second, dt, 0.10f);
    drawn->second = ease_toward(drawn->second, goal->second, dt, 0.08f);
    drawn->amp = ease_toward(drawn->amp, goal->amp, dt, 0.10f);
    drawn->mote = ease_toward(drawn->mote, goal->mote, dt, 0.10f);
    drawn->warmth = ease_toward(drawn->warmth, goal->warmth, dt, 0.16f);
    drawn->disk = ease_toward(drawn->disk, goal->disk, dt, 0.18f);
    drawn->sway = ease_toward(drawn->sway, goal->sway, dt, 0.08f);
    drawn->sway_y = ease_toward(drawn->sway_y, goal->sway_y, dt, 0.08f);
    drawn->gather = ease_toward(drawn->gather, goal->gather, dt, 0.12f);
    drawn->bead = goal->bead;
    drawn->bead_angle = goal->bead_angle;
    drawn->bead_free = goal->bead_free;
    if (goal->bead_free) {
        drawn->bead_x = ease_toward(drawn->bead_x, goal->bead_x, dt, 0.045f);
        drawn->bead_y = ease_toward(drawn->bead_y, goal->bead_y, dt, 0.045f);
    }
    drawn->light_angle = goal->light_angle;
    drawn->arc = goal->arc;
    drawn->marks = goal->marks;
    drawn->marks_lit = goal->marks_lit;
    drawn->numeral = goal->numeral;
    drawn->numeral_light = goal->numeral_light;
    drawn->lamp = goal->lamp;
}

void glass_paint(uint16_t *rgb565, uint8_t *heat, const glass_look *look)
{
    int cx;
    int cy;
    int radius;
    int glow;
    int body;
    int i;
    int motes;
    int mote_h;

    paint_init();
    memset(heat, 0, (size_t)GLASS_W * GLASS_H);
    memset(rgb565, 0, (size_t)GLASS_W * GLASS_H * sizeof(uint16_t));

    cx = CX + (int)look->sway;
    cy = CY + (int)look->sway_y;
    radius = (int)look->radius;
    glow = 16 - (int)(look->gather * 4.f);
    if (glow < 10) {
        glow = 10;
    }
    body = clampi((int)(look->amp * 110.f), 0, 140);

    moon(heat, cx, cy, look->disk);
    ring(heat, cx, cy, (int)look->radius_lag, thicken((int)look->radius_lag, glow + 2), body / 4);
    ring(heat, cx, cy, radius, thicken(radius, glow), body);
    if (look->second > 0.05f) {
        ring(heat, cx, cy, (int)look->radius_second, thicken((int)look->radius_second, glow), (int)(look->second * (float)body));
    }

    motes = look->gather > 0.75f ? 4 : 8;
    mote_h = (int)(look->mote * (1.f - look->gather * 0.7f) * 150.f);
    for (i = 0; i < motes; i++) {
        int x;
        int y;
        polar(cx, cy, ang_index(look->light_angle) + i * (STEPS / motes), radius, &x, &y);
        stamp(heat, x, y, 3, mote_h);
    }
    if (look->bead > 0.04f) {
        int x;
        int y;
        if (look->bead_free) {
            x = (int)look->bead_x;
            y = (int)look->bead_y;
        } else {
            polar(cx, cy, ang_index(look->bead_angle), (int)(look->radius * 1.02f), &x, &y);
        }
        stamp(heat, x, y, look->bead_free ? 30 : 6, (int)(look->bead * 230.f));
    }
    if (look->marks > 0.5f) {
        marks(heat, cx, cy, look->marks_lit);
    }
    if (look->arc > 0.05f) {
        int n = clampi((int)(look->arc * 28.f), 1, 40);
        int mid = ang_index(look->light_angle);
        int k;
        for (k = -n; k <= n; k++) {
            int x;
            int y;
            polar(cx, cy, mid + k, 226, &x, &y);
            stamp(heat, x, y, 3, (int)(look->arc * 170.f));
        }
    }
    if (look->numeral > 0 && look->numeral_light > 0.02f) {
        numerals(heat, cx, cy, look->numeral, look->numeral_light);
    }

    quantize(rgb565, heat, look->warmth);
}
