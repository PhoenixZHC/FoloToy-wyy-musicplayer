#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QQMUSIC_QR_SIDE_MAX 180
/* 有界inflate由平台注入；必须验证zlib校验和且输出长度精确匹配。 */
typedef bool (*qqmusic_qr_inflate_fn)(const uint8_t *input,size_t input_size,
                                     uint8_t *output,size_t output_size);
/* 接受1-bit黑白调色板PNG，输出8-bit灰度，不缩放/反色。所有临时分配有界。 */
bool qqmusic_qr_png_decode(const uint8_t *png,size_t size,qqmusic_qr_inflate_fn inflate,
                          uint8_t *pixels,size_t capacity,unsigned *width,unsigned *height);
