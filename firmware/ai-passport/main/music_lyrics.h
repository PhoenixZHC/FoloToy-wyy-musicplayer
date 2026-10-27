#pragma once
#include <stddef.h>
#include <stdint.h>
/* Copy the latest timed LRC line, accepting multiple timestamps and UTF-8. */
void music_lyrics_line(const char *lrc, uint32_t elapsed_ms, char *out, size_t capacity);
