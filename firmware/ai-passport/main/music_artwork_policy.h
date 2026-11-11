#pragma once
#include <stdbool.h>
#include <stddef.h>
/* 仅允许音乐平台图片域名，输出当前显示尺寸所需的 HTTPS URL。 */
bool music_artwork_request_url(const char *url,char *out,size_t capacity);
