idf_component_register(
    SRCS "src/homey_map.cpp" "src/homey_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        ha_client
        esp_http_client
        espressif__cjson
)
