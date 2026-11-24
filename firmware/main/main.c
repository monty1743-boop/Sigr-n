#include "glass.h"

#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "sigrun";

#define BTN_PWR GPIO_NUM_3
#define BTN_BOOT GPIO_NUM_0

static int button_edge(gpio_num_t pin, int *level)
{
    int now = gpio_get_level(pin);
    int edge = (*level == 1 && now == 0);
    *level = now;
    return edge;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_PWR) | (1ULL << BTN_BOOT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display did not start");
        return;
    }
    ESP_ERROR_CHECK(bsp_display_brightness_set(35));

    float *acc = heap_caps_calloc(GLASS_W * GLASS_H * 3, sizeof(float), MALLOC_CAP_SPIRAM);
    uint16_t *px = heap_caps_malloc(GLASS_W * GLASS_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (acc == NULL || px == NULL) {
        ESP_LOGE(TAG, "no psram for the picture");
        return;
    }

    ESP_ERROR_CHECK(bsp_display_lock((uint32_t)-1));
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_draw_buf_t *draw = lv_draw_buf_create(GLASS_W, GLASS_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    lv_obj_t *canvas = lv_canvas_create(screen);
    lv_canvas_set_draw_buf(canvas, draw);
    lv_obj_center(canvas);
    bsp_display_unlock();

    sigrun glass;
    glass_sense sense;
    int pwr = 1;
    int boot = 1;
    uint32_t last = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    sigrun_init(&glass);
    glass_render_init();
    memset(&sense, 0, sizeof(sense));

    while (1) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint32_t dt = now - last;
        if (dt < 16) {
            vTaskDelay(pdMS_TO_TICKS(16 - dt));
            now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            dt = now - last;
        }
        last = now;
        if (dt > 80) {
            dt = 80;
        }

        memset(&sense, 0, sizeof(sense));
        sense.now_ms = now;
        sense.rescue = button_edge(BTN_PWR, &pwr) || button_edge(BTN_BOOT, &boot);
        sigrun_step(&glass, &sense, dt);
        glass_render(acc, &glass.look, now, (float)dt / 1000.f);
        glass_quantize(acc, px, NULL);

        if (bsp_display_lock(20) == ESP_OK) {
            lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
            if (buf != NULL && buf->data != NULL) {
                memcpy(buf->data, px, GLASS_W * GLASS_H * sizeof(uint16_t));
                lv_obj_invalidate(canvas);
            }
            bsp_display_unlock();
        }
    }
}
