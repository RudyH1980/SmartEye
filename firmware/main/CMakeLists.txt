idf_component_register(
    SRCS "test.cpp" "ui.cpp" "main.cpp" "portal_demo.cpp" "setup_screen.cpp" "home_screen.cpp" "portal_apps.cpp" "lights_pages.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        portal_ui
        wifi_manager
        board_drivers
        power_manager
        lvgl_wrapper
        imu_services
        rtc_services
        sd_services
)
