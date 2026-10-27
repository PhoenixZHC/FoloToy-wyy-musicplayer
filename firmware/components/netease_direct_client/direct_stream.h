#pragma once
#include "netease_direct_client.h"

/* Incremental projection: validate the entire response without retaining it. */
typedef enum { DIRECT_STREAM_PLAYLISTS, DIRECT_STREAM_IDS, DIRECT_STREAM_SONGS } direct_stream_mode_t;
typedef struct {
    char kind;
    unsigned state, role, fields;
    char key[32];
} direct_json_frame_t;
typedef struct {
    direct_stream_mode_t mode;
    direct_json_frame_t frames[24];
    size_t depth, token_len, bytes;
    char token[2048];
    bool string, escape, scalar, overflow, done, found, code_ok;
    unsigned unicode_left;
    esp_err_t error;
    uint32_t offset, limit, total;
    size_t id_count;
    int64_t ids[NETEASE_DIRECT_MAX_TRACKS];
    int64_t *all_ids;
    size_t all_ids_capacity;
    uint32_t all_ids_offset;
    netease_direct_playlist_list_t *playlists;
    netease_direct_track_list_t *tracks;
    netease_direct_playlist_t item;
    netease_direct_track_t track;
    char cover_url[256];
} direct_stream_t;
void direct_stream_init(direct_stream_t *s, direct_stream_mode_t mode,
                        netease_direct_playlist_list_t *playlists, uint32_t offset, uint32_t limit);
esp_err_t direct_stream_feed(direct_stream_t *s, const char *data, size_t length);
esp_err_t direct_stream_finish(direct_stream_t *s);
