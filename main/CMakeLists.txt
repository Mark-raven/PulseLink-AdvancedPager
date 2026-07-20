idf_component_register(SRCS "ssd1306.c" "app_bt.c"
                    PRIV_REQUIRES bt nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_ledc
                    INCLUDE_DIRS ".")
