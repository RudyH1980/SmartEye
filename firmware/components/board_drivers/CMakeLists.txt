idf_component_register(
    SRCS
        "src/board_drivers.cpp"
        "src/lcd_st7701_rgb.cpp"
        "src/gt911_simple.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_lcd
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
        esp_driver_ledc
        espressif__esp_io_expander_tca9554
        waveshare__qmi8658
        waveshare__pcf85063a
        sd_services
)
