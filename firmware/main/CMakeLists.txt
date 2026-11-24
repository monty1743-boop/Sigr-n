idf_component_register(
    SRCS "main.c"
         "../../src/glass_picture.c"
         "../../src/glass_render.c"
         "../../src/glass_logic.c"
    INCLUDE_DIRS "." "../../src"
    REQUIRES nvs_flash esp_driver_gpio waveshare__esp32_s3_touch_amoled_1_75c lvgl__lvgl
)
