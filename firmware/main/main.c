#include "board.h"
#include "glass.h"

#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "sigrun";

static void load_minutes(sigrun *glass)
{
    nvs_handle_t handle;
    uint8_t mins[3] = {5, 10, 25};
    size_t len = sizeof(mins);

    if (nvs_open("sigrun", NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_blob(handle, "mins", mins, &len) != ESP_OK || len != sizeof(mins)) {
            mins[0] = 5;
            mins[1] = 10;
            mins[2] = 25;
        }
        nvs_close(handle);
    }
    sigrun_set_minutes(glass, mins[0], mins[1], mins[2]);
}

static void store_minutes(const sigrun *glass)
{
    nvs_handle_t handle;
    uint8_t mins[3];

    mins[0] = (uint8_t)glass->task_min[0];
    mins[1] = (uint8_t)glass->task_min[1];
    mins[2] = (uint8_t)glass->task_min[2];
    if (nvs_open("sigrun", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(handle, "mins", mins, sizeof(mins)) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static glass_look s_pub;
static sigrun_scene s_scene;
static sigrun_source s_source;
static volatile uint32_t s_frame_ms;
static volatile int s_input;
static glass_sense s_sense;

static const char *scene_name(sigrun_scene scene)
{
    switch (scene) {
    case SIGRUN_SIGH:
        return "sigh";
    case SIGRUN_GROUND:
        return "ground";
    case SIGRUN_LIST:
        return "list";
    default:
        return "tide";
    }
}

static const char *source_name(sigrun_source source)
{
    switch (source) {
    case SIGRUN_SRC_FINGER:
        return "finger";
    case SIGRUN_SRC_WALK:
        return "walk";
    case SIGRUN_SRC_SWAY:
        return "sway";
    case SIGRUN_SRC_BREATH:
        return "breath";
    default:
        return "clock";
    }
}

static void sense_task(void *arg)
{
    sigrun glass;
    glass_body body;
    board_sample sample;
    uint32_t last;
    uint32_t logged;

    (void)arg;
    sigrun_init(&glass);
    load_minutes(&glass);
    glass_body_init(&body);
    portENTER_CRITICAL(&s_mux);
    s_pub = glass.look;
    portEXIT_CRITICAL(&s_mux);

    last = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    logged = last;
    while (1) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint32_t dt = now - last;

        if (dt < 20u) {
            vTaskDelay(pdMS_TO_TICKS(20u - dt));
            now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            dt = now - last;
        }
        last = now;
        if (dt < 1u) {
            dt = 1u;
        }
        if (dt > 80u) {
            dt = 80u;
        }

        board_read(&sample);
        if (sample.touch.down || sample.rescue) {
            s_input = 1;
        }
        memset(&s_sense, 0, sizeof(s_sense));
        s_sense.now_ms = now;
        s_sense.rescue = sample.rescue;
        s_sense.charging = sample.charging;
        s_sense.cell = sample.cell;
        if (sample.have_imu) {
            glass_body_imu(&body, &sample.imu, dt, &s_sense);
        }
        body.timer_face = glass.card >= 1 && glass.card <= 3 && glass.task_end_ms == 0;
        glass_body_touch(&body, &sample.touch, dt, &s_sense);
        if (sample.have_ear) {
            glass_body_ear(&body, &sample.ear, dt, &s_sense);
        }
        {
            int ending = glass.task_end_ms > 0;
            sigrun_step(&glass, &s_sense, dt);
            if (!ending && glass.task_end_ms > 0) {
                board_chime();
            }
            if (glass.minutes_dirty) {
                store_minutes(&glass);
                glass.minutes_dirty = 0;
            }
        }

        portENTER_CRITICAL(&s_mux);
        s_pub = glass.look;
        s_scene = glass.scene;
        s_source = glass.source;
        portEXIT_CRITICAL(&s_mux);
        board_noise(glass.noise);
        board_tick(glass.tick);

        if ((now - logged) >= 2000u) {
            logged = now;
            ESP_LOGI(TAG,
                     "%s %s ax=%.2f ay=%.2f az=%.2f walk=%d sway=%.2f still=%.2f bead=%.2f per=%.1f quiet=%d "
                     "breath=%.2f hum=%.2f chg=%d frame=%ums",
                     scene_name(s_scene), source_name(s_source), sample.imu.ax, sample.imu.ay, sample.imu.az,
                     s_sense.walking, s_sense.sway, s_sense.still, s_sense.fidget, s_sense.finger_period,
                     s_sense.room_quiet, s_sense.near_breath, s_sense.hum, s_sense.charging, (unsigned)s_frame_ms);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    uint8_t *heat;
    uint16_t *px;
    lv_obj_t *canvas;

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display did not start");
        return;
    }
    ESP_ERROR_CHECK(bsp_display_brightness_set(35));
    ESP_LOGI(TAG, "build dial=156 digits");

    heat = heap_caps_malloc(GLASS_W * GLASS_H, MALLOC_CAP_SPIRAM);
    px = heap_caps_malloc(GLASS_W * GLASS_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (heat == NULL || px == NULL) {
        ESP_LOGE(TAG, "no memory for the picture");
        return;
    }
    {
        glass_look probe;
        int y;
        int y0 = -1;
        int y1 = -1;
        int gaps = 0;

        glass_look_clear(&probe);
        probe.numeral = 53;
        probe.numeral_light = 1.f;
        probe.amp = 1.f;
        probe.disk = 1.f;
        probe.radius = 140.f;
        probe.radius_lag = 140.f;
        glass_paint(px, heat, &probe);
        for (y = 0; y < GLASS_H; y++) {
            int x;
            for (x = 270; x < 340; x++) {
                if (heat[y * GLASS_W + x] > 150) {
                    if (y0 < 0) {
                        y0 = y;
                    }
                    y1 = y;
                    break;
                }
            }
        }
        for (y = y0; y <= y1; y++) {
            int x;
            int lit = 0;
            for (x = 270; x < 340; x++) {
                if (heat[y * GLASS_W + x] > 150) {
                    lit = 1;
                }
            }
            if (!lit) {
                gaps++;
            }
        }
        ESP_LOGI(TAG, "mark53 gaps=%d span=%d", gaps, y1 - y0);
    }

    ESP_ERROR_CHECK(bsp_display_lock((uint32_t)-1));
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_draw_buf_t *draw = lv_draw_buf_create(GLASS_W, GLASS_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    canvas = lv_canvas_create(screen);
    lv_canvas_set_draw_buf(canvas, draw);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(canvas);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    bsp_display_unlock();

    board_init();
    if (xTaskCreate(sense_task, "sigrun_sense", 6144, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "sense task did not start");
        return;
    }

    static glass_look shown;
    static uint32_t pace_ms = 16;
    static uint32_t idle_ms = 0;
    static uint32_t panel_ms = 0;
    static int bright = 35;
    static int powered_off = 0;

    while (1) {
        glass_look look;
        uint32_t started;
        uint32_t frame_dt;
        uint32_t now;
        uint32_t dt;

        now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        dt = panel_ms == 0 ? 0 : now - panel_ms;
        if (dt > 200u) {
            dt = 200u;
        }
        panel_ms = now;
        if (s_input) {
            s_input = 0;
            idle_ms = 0;
        } else {
            idle_ms += dt;
        }
        if (!powered_off && idle_ms >= 3600000u) {
            powered_off = 1;
            board_power_off();
        }

        portENTER_CRITICAL(&s_mux);
        look = s_pub;
        portEXIT_CRITICAL(&s_mux);

        started = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        glass_look_ease(&shown, &look, pace_ms);
        glass_paint(px, heat, &shown);
        frame_dt = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - started;
        s_frame_ms = frame_dt;
        pace_ms = frame_dt < 16u ? 16u : frame_dt;
        if (pace_ms > 120u) {
            pace_ms = 120u;
        }

        if (bsp_display_lock(120) == ESP_OK) {
            int next = look.rune > 0.f ? (int)(look.rune * 100.f + 0.5f) : 35 + (int)(look.lamp * 65.f + 0.5f);
            lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);

            if (next > 100) {
                next = 100;
            }
            if (next != bright) {
                bsp_display_brightness_set(next);
                bright = next;
            }

            if (buf != NULL && buf->data != NULL) {
                uint32_t stride = buf->header.stride;
                uint32_t row = GLASS_W * (uint32_t)sizeof(uint16_t);
                if (stride <= row) {
                    memcpy(buf->data, px, (size_t)row * GLASS_H);
                } else {
                    uint32_t y;
                    for (y = 0; y < GLASS_H; y++) {
                        memcpy((uint8_t *)buf->data + (size_t)y * stride, px + (size_t)y * GLASS_W, row);
                    }
                }
                lv_obj_invalidate(canvas);
            }
            bsp_display_unlock();
        }
        if (frame_dt < 33u) {
            vTaskDelay(pdMS_TO_TICKS(33u - frame_dt));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
