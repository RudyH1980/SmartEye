idf_component_register(
    SRCS "src/ha_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        espressif__cjson
)
