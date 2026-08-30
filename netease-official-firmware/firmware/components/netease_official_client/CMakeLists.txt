idf_component_register(
    SRCS "official_api_codec.c"
         "official_api_signer.c"
         "official_http.c"
         "netease_official_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json mbedtls
)
