#pragma once
#include <stddef.h>
#include <stdint.h>
typedef enum {
    MUSIC_LYRICS_EMPTY, MUSIC_LYRICS_TIMED, MUSIC_LYRICS_UNTIMED, MUSIC_LYRICS_INSTRUMENTAL,
} music_lyrics_kind_t;
music_lyrics_kind_t music_lyrics_classify(const char *lrc);
/* Copy the latest timed LRC line, accepting multiple timestamps and UTF-8. */
void music_lyrics_line(const char *lrc, uint32_t elapsed_ms, char *out, size_t capacity);
