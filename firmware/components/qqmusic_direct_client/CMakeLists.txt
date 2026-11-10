idf_component_register(SRCS "qqmusic_mqtt.c" "qqmusic_login.c" "qqmusic_qr_png.c"
    "qqmusic_json.c" "qqmusic_parse.c" "qqmusic_client.c" INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls tcp_transport esp_timer json mbedtls esp_hw_support nvs_flash)
