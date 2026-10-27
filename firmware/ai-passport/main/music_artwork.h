#pragma once
#include "esp_err.h"
#include "lvgl.h"
/* Worker-only download/decode; caller owns and frees image->data after UI detach. */
esp_err_t music_artwork_load(const char *url, lv_image_dsc_t *image);
