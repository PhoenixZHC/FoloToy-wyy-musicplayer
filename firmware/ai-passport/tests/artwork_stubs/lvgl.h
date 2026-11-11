#pragma once
#include <stdint.h>
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_RGB565 1
typedef struct {
    struct { uint32_t magic,cf,w,h,stride; } header;
    const uint8_t *data;
    uint32_t data_size;
} lv_image_dsc_t;
