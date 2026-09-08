idf_component_register(
    SRCS "src/wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        nvs_flash
)
