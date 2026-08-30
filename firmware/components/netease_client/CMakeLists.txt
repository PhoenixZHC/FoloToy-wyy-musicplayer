idf_component_register(
    SRCS "netease_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json mbedtls
)
