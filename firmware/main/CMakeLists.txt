idf_component_register(
    SRCS "main.c"
         "board.c"
         "../../src/glass_picture.c"
         "../../src/glass_paint.c"
         "../../src/glass_logic.c"
         "../../src/glass_body.c"
    INCLUDE_DIRS "." "../../src"
    REQUIRES nvs_flash esp_driver_gpio esp_driver_i2c waveshare__esp32_s3_touch_amoled_1_75c lvgl__lvgl espressif__esp_codec_dev
)
