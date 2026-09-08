idf_component_register(
    SRCS "lvgl_wrapper.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        board_drivers
        esp_lcd
        esp_lvgl_port
        lvgl
)
