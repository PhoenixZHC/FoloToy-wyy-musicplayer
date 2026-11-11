idf_component_register(SRCS "bounded_http.c" INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp_timer lwip freertos)
