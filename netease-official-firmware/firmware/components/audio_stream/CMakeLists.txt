idf_component_register(
    SRCS "audio_stream.c" "mp3_decoder_helix.c"
    INCLUDE_DIRS "include" "."
    REQUIRES bsp esp_http_client esp-tls
)
