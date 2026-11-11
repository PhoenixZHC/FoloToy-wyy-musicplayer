#pragma once
#include "netease_client.h"
#include "qqmusic_client.h"

typedef enum { MUSIC_PLATFORM_NETEASE=0, MUSIC_PLATFORM_QQ=1 } music_platform_t;
music_platform_t music_provider_platform(void);
const char *music_provider_name(void);
const char *music_provider_account(void);
esp_err_t music_provider_open(music_platform_t platform);
void music_provider_close(void);
esp_err_t music_provider_restore(void);
esp_err_t music_provider_logout(void);
void music_provider_nickname(char *out,size_t capacity);
esp_err_t music_provider_playlists(netease_playlist_list_t *out);
esp_err_t music_provider_tracks(int64_t id,uint32_t limit,uint32_t offset,netease_track_list_t *out);
esp_err_t music_provider_url(const netease_track_t *track,int bitrate,netease_track_url_t *out);
esp_err_t music_provider_cover_url(const netease_track_t *track,char *out,size_t capacity);
esp_err_t music_provider_lyrics(const netease_track_t *track,char *out,size_t capacity);
qqmusic_client_t *music_provider_qq_client(void);
