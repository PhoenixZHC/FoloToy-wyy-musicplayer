#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 借用原始响应的只读切片，不分配JSON对象树。 */
typedef struct { const char *text; size_t length; } qqmusic_json_t;
bool qqmusic_json_parse(const char *text, size_t length, qqmusic_json_t *out);
bool qqmusic_json_member(qqmusic_json_t object, const char *key, qqmusic_json_t *out);
bool qqmusic_json_index(qqmusic_json_t array, size_t index, qqmusic_json_t *out);
bool qqmusic_json_string(qqmusic_json_t value, char *out, size_t capacity);
/* 仅用于显示文字，完整验证后按UTF-8边界缩短；资源标识和凭据禁止使用此接口。 */
bool qqmusic_json_display_string(qqmusic_json_t value, char *out, size_t capacity);
bool qqmusic_json_uint(qqmusic_json_t value, uint64_t *out);
bool qqmusic_json_bool(qqmusic_json_t value, bool *out);
