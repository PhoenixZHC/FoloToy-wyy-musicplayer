idf_component_register(
    SRCS "netease_direct_client.c" "direct_stream.c" "direct_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json mbedtls nvs_flash esp_hw_support
)
