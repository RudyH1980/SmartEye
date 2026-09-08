idf_component_register(
    SRCS "src/sd_services.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        fatfs
        sdmmc
        esp_driver_sdmmc
        esp_timer
        board_drivers
)
