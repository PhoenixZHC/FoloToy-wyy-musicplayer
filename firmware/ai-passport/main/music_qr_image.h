#pragma once
#include "esp_err.h"
#include "lvgl.h"
#include <stddef.h>
esp_err_t music_qr_image_decode(const unsigned char *png,size_t length,lv_image_dsc_t *out);
/* 调用方持有 LVGL 锁，先移除引用该图片的控件。 */
void music_qr_image_clear(lv_image_dsc_t *image);
