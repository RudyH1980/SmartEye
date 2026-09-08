idf_component_register(
    SRCS
        "src/screen_orientation.cpp"
        "src/motion_detection.cpp"
        "src/gaming_control.cpp"
        "src/pedometer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        board_drivers
        waveshare__qmi8658
        freertos
)
