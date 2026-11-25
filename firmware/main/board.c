#include "board.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "sigrun";

#define BOOT_GPIO GPIO_NUM_0

#define AXP_ADDR 0x34
#define AXP_CHIP_ID 0x4A
#define AXP_REG_ID 0x03
#define AXP_REG_STATUS1 0x00
#define AXP_REG_STATUS2 0x01
#define AXP_REG_COMMON 0x10
#define AXP_SOFT_OFF 0x01
#define AXP_REG_PWROFF_EN 0x22
#define AXP_REG_ADC 0x30
#define AXP_REG_VBAT_H 0x34
#define AXP_REG_VBAT_L 0x35
#define AXP_REG_INTEN2 0x41
#define AXP_REG_INTSTS2 0x49
#define AXP_PKEY_LONG (1u << 2)
#define AXP_PKEY_SHORT (1u << 3)

#define QMI_ADDR_L 0x6B
#define QMI_ADDR_H 0x6A
#define QMI_WHOAMI 0x05
#define QMI_REG_WHOAMI 0x00
#define QMI_REG_CTRL1 0x02
#define QMI_REG_CTRL2 0x03
#define QMI_REG_CTRL3 0x04
#define QMI_REG_CTRL5 0x06
#define QMI_REG_CTRL7 0x08
#define QMI_REG_CTRL8 0x09
#define QMI_REG_RST_RESULT 0x4D
#define QMI_REG_RESET 0x60
#define QMI_REG_AX 0x35
#define QMI_ACC_SCALE (4.f / 32768.f)
#define QMI_GYR_SCALE (256.f / 32768.f)

#define EAR_RATE 16000
#define EAR_FRAMES 256

static i2c_master_dev_handle_t s_axp;
static i2c_master_dev_handle_t s_qmi;
static int s_axp_ok;
static int s_qmi_ok;
static int s_boot_level = 1;
static lv_indev_t *s_indev;
static int s_touch_down;
static float s_touch_x;
static float s_touch_y;
static volatile float s_rms;
static volatile float s_low;
static volatile int s_have_ear;
static int s_ear_channels;

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 50);
}

static float cell_level(void);

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};

    return i2c_master_transmit(dev, buf, sizeof(buf), 50);
}

static int add_dev(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };

    return i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, out) == ESP_OK;
}

static void axp_init(void)
{
    uint8_t id = 0;
    uint8_t en = 0;

    if (i2c_master_probe(bsp_i2c_get_handle(), AXP_ADDR, 50) != ESP_OK || !add_dev(AXP_ADDR, &s_axp)) {
        ESP_LOGW(TAG, "power chip stayed quiet");
        return;
    }
    if (reg_read(s_axp, AXP_REG_ID, &id, 1) != ESP_OK || id != AXP_CHIP_ID) {
        ESP_LOGW(TAG, "power chip id 0x%02x", id);
        return;
    }
    if (reg_read(s_axp, AXP_REG_INTEN2, &en, 1) == ESP_OK) {
        en = (uint8_t)(en | AXP_PKEY_SHORT);
        reg_write(s_axp, AXP_REG_INTEN2, en);
    }
    reg_write(s_axp, AXP_REG_INTSTS2, 0xFF);
    {
        uint8_t off = 0;
        if (reg_read(s_axp, AXP_REG_PWROFF_EN, &off, 1) != ESP_OK) {
            off = 0;
        }
        /* Long press powers off. Bit 0 set would restart instead. */
        off = (uint8_t)((off & ~0x01u) | 0x02u);
        reg_write(s_axp, AXP_REG_PWROFF_EN, off);
    }
    {
        uint8_t adc = 0;
        if (reg_read(s_axp, AXP_REG_ADC, &adc, 1) == ESP_OK) {
            reg_write(s_axp, AXP_REG_ADC, (uint8_t)(adc | 0x01u));
        }
    }
    s_axp_ok = 1;
    ESP_LOGI(TAG, "power chip ready");
}

static int qmi_ok_id(uint8_t addr)
{
    uint8_t id = 0;

    if (i2c_master_probe(bsp_i2c_get_handle(), addr, 50) != ESP_OK || !add_dev(addr, &s_qmi)) {
        return 0;
    }
    if (reg_read(s_qmi, QMI_REG_WHOAMI, &id, 1) == ESP_OK && id == QMI_WHOAMI) {
        return 1;
    }
    return 0;
}

static void qmi_init(void)
{
    uint8_t ctrl1 = 0;
    uint8_t result = 0;
    int i;

    if (!qmi_ok_id(QMI_ADDR_L) && !qmi_ok_id(QMI_ADDR_H)) {
        ESP_LOGW(TAG, "motion stayed quiet");
        return;
    }
    reg_write(s_qmi, QMI_REG_RESET, 0xB0);
    for (i = 0; i < 12; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (reg_read(s_qmi, QMI_REG_RST_RESULT, &result, 1) == ESP_OK && result == 0x80) {
            break;
        }
    }
    if (reg_read(s_qmi, QMI_REG_CTRL1, &ctrl1, 1) != ESP_OK) {
        ctrl1 = 0;
    }
    reg_write(s_qmi, QMI_REG_CTRL1, (uint8_t)(ctrl1 | 0x40));
    reg_write(s_qmi, QMI_REG_CTRL7, 0x00);
    reg_write(s_qmi, QMI_REG_CTRL8, 0x80);
    reg_write(s_qmi, QMI_REG_CTRL2, 0x16);
    reg_write(s_qmi, QMI_REG_CTRL3, 0x46);
    reg_write(s_qmi, QMI_REG_CTRL5, 0x11);
    reg_write(s_qmi, QMI_REG_CTRL7, 0x03);
    vTaskDelay(pdMS_TO_TICKS(30));
    s_qmi_ok = 1;
    ESP_LOGI(TAG, "motion ready");
}

static int16_t s16le(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int read_imu(glass_imu *imu)
{
    uint8_t raw[12];

    if (!s_qmi_ok || reg_read(s_qmi, QMI_REG_AX, raw, sizeof(raw)) != ESP_OK) {
        return 0;
    }
    imu->ax = (float)s16le(raw + 0) * QMI_ACC_SCALE;
    imu->ay = (float)s16le(raw + 2) * QMI_ACC_SCALE;
    imu->az = (float)s16le(raw + 4) * QMI_ACC_SCALE;
    imu->gx = (float)s16le(raw + 6) * QMI_GYR_SCALE;
    imu->gy = (float)s16le(raw + 8) * QMI_GYR_SCALE;
    imu->gz = (float)s16le(raw + 10) * QMI_GYR_SCALE;
    return 1;
}

static void read_power(board_sample *out)
{
    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t irq = 0;
    int charge;

    if (!s_axp_ok) {
        return;
    }
    if (reg_read(s_axp, AXP_REG_INTSTS2, &irq, 1) == ESP_OK && irq != 0) {
        reg_write(s_axp, AXP_REG_INTSTS2, 0xFF);
        if ((irq & AXP_PKEY_SHORT) && !(irq & AXP_PKEY_LONG)) {
            out->rescue = 1;
        }
    }
    if (reg_read(s_axp, AXP_REG_STATUS1, &status1, 1) != ESP_OK) {
        return;
    }
    if (reg_read(s_axp, AXP_REG_STATUS2, &status2, 1) != ESP_OK) {
        return;
    }
    charge = status2 & 0x07;
    out->charging = (charge >= 1 && charge <= 4) || (status1 & 0x20) != 0;
    out->cell = out->charging ? 1.f : cell_level();
}

static int batt_mv(void)
{
    uint8_t hi = 0;
    uint8_t lo = 0;

    if (!s_axp_ok) {
        return 0;
    }
    if (reg_read(s_axp, AXP_REG_VBAT_H, &hi, 1) != ESP_OK) {
        return 0;
    }
    if (reg_read(s_axp, AXP_REG_VBAT_L, &lo, 1) != ESP_OK) {
        return 0;
    }
    return ((int)(hi & 0x1Fu) << 8) | lo;
}

static float cell_level(void)
{
    static int n;
    static float level = 1.f;
    int mv;

    n++;
    if (n < 50 && level > 0.f) {
        return level;
    }
    n = 0;
    mv = batt_mv();
    if (mv < 2500) {
        return level;
    }
    level = (mv - 3450.f) / (4100.f - 3450.f);
    if (level < 0.f) {
        level = 0.f;
    }
    if (level > 1.f) {
        level = 1.f;
    }
    return level;
}

void board_latch_touch(void)
{
    lv_point_t point;

    if (s_indev == NULL) {
        return;
    }
    /* The finger is sampled on the sense beat. Taking the display lock here
     * waits out a panel flush and drops the sample. A torn point is rejected
     * later as a teleport. */
    lv_indev_get_point(s_indev, &point);
    s_touch_down = lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED;
    s_touch_x = (float)point.x;
    s_touch_y = (float)point.y;
}

static void read_touch(glass_touch *touch)
{
    touch->down = s_touch_down;
    touch->x = s_touch_x;
    touch->y = s_touch_y;
}

static int open_ear(esp_codec_dev_handle_t mic)
{
    static const uint8_t channels[] = {2, 4, 1};
    size_t i;

    for (i = 0; i < sizeof(channels); i++) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = channels[i],
            .channel_mask = (uint16_t)((1u << channels[i]) - 1u),
            .sample_rate = EAR_RATE,
        };

        if (esp_codec_dev_open(mic, &fs) == ESP_CODEC_DEV_OK) {
            s_ear_channels = channels[i];
            esp_codec_dev_set_in_gain(mic, 28.f);
            return 1;
        }
    }
    return 0;
}

static void ear_features(const int16_t *raw, size_t frames, int channels)
{
    float lp = 0.f;
    double energy = 0.0;
    double low_energy = 0.0;
    size_t i;
    const float blend = 0.07f;

    for (i = 0; i < frames; i++) {
        float sample;
        int32_t mixed;

        if (channels == 1) {
            mixed = raw[i];
        } else if (channels == 4) {
            mixed = ((int32_t)raw[i * 4] + (int32_t)raw[i * 4 + 2]) / 2;
        } else {
            mixed = ((int32_t)raw[i * channels] + (int32_t)raw[i * channels + 1]) / 2;
        }
        sample = (float)mixed / 32768.f;
        lp += blend * (sample - lp);
        energy += (double)sample * (double)sample;
        low_energy += (double)lp * (double)lp;
    }
    if (frames == 0) {
        return;
    }
    s_rms = sqrtf((float)(energy / (double)frames));
    s_low = sqrtf((float)(low_energy / (double)frames));
    s_have_ear = 1;
}

static void ear_task(void *arg)
{
    esp_codec_dev_handle_t mic = arg;
    int16_t *raw = malloc(EAR_FRAMES * 4 * sizeof(int16_t));
    int warned = 0;

    if (raw == NULL) {
        ESP_LOGW(TAG, "no memory for the microphones");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        int want = EAR_FRAMES * s_ear_channels * (int)sizeof(int16_t);

        if (esp_codec_dev_read(mic, raw, want) != ESP_CODEC_DEV_OK) {
            if (!warned) {
                ESP_LOGW(TAG, "microphone read failed");
                warned = 1;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        ear_features(raw, EAR_FRAMES, s_ear_channels);
    }
}

static void ear_init(void)
{
    i2s_std_config_t i2s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EAR_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    esp_codec_dev_handle_t mic;

    if (bsp_audio_init(&i2s) != ESP_OK) {
        ESP_LOGW(TAG, "microphones stayed quiet");
        return;
    }
    mic = bsp_audio_codec_microphone_init();
    if (mic == NULL || !open_ear(mic)) {
        ESP_LOGW(TAG, "microphones stayed quiet");
        return;
    }
    if (xTaskCreate(ear_task, "sigrun_ear", 6144, mic, 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "microphone task did not start");
        return;
    }
    ESP_LOGI(TAG, "microphones ready (%d ch)", s_ear_channels);
}

static esp_codec_dev_handle_t s_spk;
static volatile int s_noise;
static volatile int s_chime;
static volatile int s_tick;
static int s_noise_was;
static int s_chime_at = -1;
static int s_tick_on;
static int s_tick_at;
static int s_vol = -1;
static int s_vol_noise = 73;
static int s_vol_hit = 100;

static int16_t mix_clamp(float a, float b)
{
    int v = (int)(a + b);

    if (v > 32767) {
        v = 32767;
    }
    if (v < -32767) {
        v = -32767;
    }
    return (int16_t)v;
}

static float thump(float t, int n)
{
    float atk;
    float rel;

    if (n >= 360) {
        return 0.f;
    }
    atk = t < 0.002f ? t / 0.002f : 1.f;
    rel = n > 280 ? (360 - n) / 80.f : 1.f;
    return sinf(t * 140.f * 6.2831853f) * atk * rel * 32767.f;
}

static int16_t tick_sample(int n)
{
    float t;
    float atk;
    float env;
    float click;

    if (n >= 360) {
        return 0;
    }
    t = (float)n / 16000.f;
    click = 0.f;
    if (n < 192) {
        atk = t < 0.001f ? t / 0.001f : 1.f;
        env = atk * expf(-t / 0.0032f);
        click = cosf(t * 2000.f * 6.2831853f) * env * 3700.f;
    }
    return mix_clamp(click, thump(t, n));
}

static int16_t chime_sample(int n)
{
    float t = (float)n / 16000.f;
    float u = t < 0.45f ? t / 0.45f : 1.f;
    float swell = u * u * (3.f - 2.f * u);
    float body = expf(-t * 1.15f);
    float bloom = expf(-t * 4.5f);
    float tone = sinf(t * 880.f * 6.2831853f) * body + sinf(t * 1760.f * 6.2831853f) * bloom * 0.28f;
    int v = (int)(tone * swell * 5200.f);

    if (v > 5200) {
        v = 5200;
    }
    if (v < -5200) {
        v = -5200;
    }
    return (int16_t)v;
}

static void speaker_vol(int vol)
{
    if (s_spk == NULL || vol == s_vol) {
        return;
    }
    esp_codec_dev_set_out_vol(s_spk, vol);
    s_vol = vol;
}

static int16_t noise_sample(int kind, uint32_t *rng, float *b0, float *b1, float *b2, float *brown)
{
    float white;
    float drive;
    float out;

    *rng = *rng * 1664525u + 1013904223u;
    /* High 16 bits of the generator. Filters stay on the old ±512 drive. */
    white = (float)((int)(*rng >> 16) - 32768);
    drive = white * (512.f / 32768.f);
    if (kind == 2) {
        *b0 = 0.99765f * *b0 + drive * 0.0990460f;
        *b1 = 0.96300f * *b1 + drive * 0.2965164f;
        *b2 = 0.57000f * *b2 + drive * 1.0526913f;
        out = (*b0 + *b1 + *b2 + drive * 0.1848f) * 3.5f;
    } else if (kind == 3) {
        *brown = (*brown + drive) * 0.98f;
        out = *brown * 1.3f;
    } else {
        out = white * (8000.f / 32768.f);
    }
    if (out > 8000.f) {
        out = 8000.f;
    }
    if (out < -8000.f) {
        out = -8000.f;
    }
    return (int16_t)out;
}

static void noise_task(void *arg)
{
    int16_t buf[256];
    uint32_t rng = 0x51A7u;
    float b0 = 0.f;
    float b1 = 0.f;
    float b2 = 0.f;
    float brown = 0.f;

    (void)arg;
    while (1) {
        int i;
        int kind = s_noise;

        if (s_chime) {
            s_chime = 0;
            s_chime_at = 0;
        }
        if (s_tick && !s_tick_on) {
            s_tick_on = 1;
            s_tick_at = 0;
        }
        if (!s_tick) {
            s_tick_on = 0;
        }
        gpio_set_level(BSP_POWER_AMP_IO, (kind > 0 || s_tick_on || s_chime_at >= 0 || s_chime) ? 1 : 0);
        speaker_vol((kind <= 0 && (s_chime_at >= 0 || s_tick_on || s_chime)) ? s_vol_hit : s_vol_noise);
        if (kind > 0) {
            s_chime_at = -1;
        } else if (s_chime_at >= 0) {
            for (i = 0; i < 256; i += 2) {
                int16_t sample = chime_sample(s_chime_at++);
                buf[i] = sample;
                buf[i + 1] = sample;
            }
            if (s_chime_at >= 48000) {
                s_chime_at = -1;
            }
            if (esp_codec_dev_write(s_spk, buf, sizeof(buf)) != ESP_CODEC_DEV_OK) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        } else if (s_tick_on) {
            for (i = 0; i < 256; i += 2) {
                int16_t sample = tick_sample(s_tick_at++);
                if (s_tick_at >= 16000) {
                    s_tick_at = 0;
                }
                buf[i] = sample;
                buf[i + 1] = sample;
            }
            if (esp_codec_dev_write(s_spk, buf, sizeof(buf)) != ESP_CODEC_DEV_OK) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }
        if (kind <= 0) {
            if (s_noise_was) {
                memset(buf, 0, sizeof(buf));
                esp_codec_dev_write(s_spk, buf, sizeof(buf));
                s_noise_was = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        s_noise_was = 1;
        for (i = 0; i < 256; i++) {
            buf[i] = noise_sample(kind, &rng, &b0, &b1, &b2, &brown);
        }
        if (esp_codec_dev_write(s_spk, buf, sizeof(buf)) != ESP_CODEC_DEV_OK) {
            if (s_noise_was == 1) {
                ESP_LOGW(TAG, "speaker write failed");
                s_noise_was = 2;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void noise_init(void)
{
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = (uint16_t)(ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)),
        .sample_rate = 16000,
    };

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL || esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "speaker stayed quiet");
        s_spk = NULL;
        return;
    }
    esp_codec_dev_set_out_mute(s_spk, false);
    {
        esp_codec_dev_vol_map_t map[2] = {
            {.vol = 0, .db_value = -50.f},
            {.vol = 100, .db_value = 12.f},
        };
        esp_codec_dev_vol_curve_t curve = {
            .vol_map = map,
            .count = 2,
        };

        if (esp_codec_dev_set_vol_curve(s_spk, &curve) != ESP_CODEC_DEV_OK) {
            s_vol_noise = 90;
            s_vol_hit = 100;
        }
    }
    speaker_vol(s_vol_noise);
    gpio_set_direction(BSP_POWER_AMP_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    if (xTaskCreate(noise_task, "sigrun_noise", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "noise task did not start");
        return;
    }
    ESP_LOGI(TAG, "soft noise ready");
}

void board_tick(int on)
{
    s_tick = (s_spk != NULL && on) ? 1 : 0;
}

void board_chime(void)
{
    if (s_spk != NULL) {
        s_chime = 1;
    }
}

void board_power_off(void)
{
    uint8_t cfg = 0;

    gpio_set_direction(BSP_POWER_AMP_IO, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_POWER_AMP_IO, 0);
    if (!s_axp_ok) {
        return;
    }
    if (reg_read(s_axp, AXP_REG_COMMON, &cfg, 1) != ESP_OK) {
        cfg = 0;
    }
    reg_write(s_axp, AXP_REG_COMMON, (uint8_t)(cfg | AXP_SOFT_OFF));
    vTaskDelay(pdMS_TO_TICKS(100));
    reg_write(s_axp, AXP_REG_COMMON, (uint8_t)(cfg | AXP_SOFT_OFF));
}

void board_noise(int kind)
{
    if (s_spk == NULL || kind < 0) {
        s_noise = 0;
        return;
    }
    s_noise = kind > 3 ? 3 : kind;
}

static int boot_edge(void)
{
    int now = gpio_get_level(BOOT_GPIO);
    int edge = s_boot_level == 1 && now == 0;

    s_boot_level = now;
    return edge;
}

void board_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io));
    s_boot_level = gpio_get_level(BOOT_GPIO);
    s_indev = bsp_display_get_input_dev();
    axp_init();
    qmi_init();
    ear_init();
    noise_init();
}

void board_read(board_sample *out)
{
    memset(out, 0, sizeof(*out));
    board_latch_touch();
    out->rescue = boot_edge();
    read_power(out);
    out->have_imu = read_imu(&out->imu);
    read_touch(&out->touch);
    out->have_ear = s_have_ear;
    out->ear.rms = s_rms;
    out->ear.low = s_low;
}
