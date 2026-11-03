idf_component_register(
    SRCS "audio_stream.c" "audio_download.c" "audio_stream_policy.c" "mp3_decoder_helix.c"
    INCLUDE_DIRS "include" "."
    REQUIRES bsp esp_http_client esp-tls
    PRIV_REQUIRES esp_timer
)
