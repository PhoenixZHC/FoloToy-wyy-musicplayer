#pragma once
#include "qqmusic_json.h"

#if defined(QQMUSIC_HOST_TEST)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_RESPONSE 0x108
#else
#include "esp_err.h"
#endif

#define QQMUSIC_ERR_AUTH ((esp_err_t)0x7d01)
#define QQMUSIC_ERR_UNAVAILABLE ((esp_err_t)0x7d02)
#define QQMUSIC_ERR_REJECTED ((esp_err_t)0x7d03)
#define QQMUSIC_SESSION_MAX 8192
#define QQMUSIC_MAX_PLAYLISTS 20
#define QQMUSIC_MAX_TRACKS 8
#define QQMUSIC_MID_MAX 32
#define QQMUSIC_URL_MAX 1024

typedef struct {
    uint64_t id; /* 腾讯真实disstid；喜欢目录使用接口规定的0及dir_id=201。 */
    uint32_t dir_id, total;
    char name[128];
} qqmusic_playlist_t;
typedef struct {
    size_t count;
    bool has_more;
    qqmusic_playlist_t items[QQMUSIC_MAX_PLAYLISTS];
} qqmusic_playlists_t;
typedef struct {
    char mid[QQMUSIC_MID_MAX], media_mid[QQMUSIC_MID_MAX], album_mid[QQMUSIC_MID_MAX];
    char name[128], artist[96];
    uint32_t song_type, duration_ms;
    uint64_t mp3_bytes;
} qqmusic_track_t;
typedef struct {
    size_t count;
    uint32_t total, next_offset;
    uint64_t revision;
    bool has_more;
    qqmusic_track_t items[QQMUSIC_MAX_TRACKS];
} qqmusic_tracks_t;
typedef struct {
    char url[QQMUSIC_URL_MAX];
    uint64_t full_bytes;
    uint32_t expires_seconds;
} qqmusic_audio_t;
typedef struct {
    char *credential; /* 唯一内存会话，close负责清零释放；不得跨worker并发访问。 */
    char account[32], nickname[64];
    bool blocked;
} qqmusic_client_t;

/* 所有输出均先清空；失败不得使用部分结果。JSON切片只借用调用方缓冲。 */
esp_err_t qqmusic_parse_credential(const char *json,size_t length,char account[32],char nickname[64]);
esp_err_t qqmusic_parse_playlists(qqmusic_json_t data,bool favorites,qqmusic_playlists_t *out);
esp_err_t qqmusic_parse_tracks(qqmusic_json_t data,uint32_t offset,qqmusic_tracks_t *out);
esp_err_t qqmusic_parse_audio(qqmusic_json_t data,qqmusic_json_t dispatch,const qqmusic_track_t *track,qqmusic_audio_t *out);
esp_err_t qqmusic_parse_response(const char *json,size_t length,qqmusic_json_t *data);

/* 以下API仅在联网worker中调用；不初始化或释放共享Wi-Fi。 */
void qqmusic_client_init(qqmusic_client_t *client);
void qqmusic_client_close(qqmusic_client_t *client);
esp_err_t qqmusic_client_accept(qqmusic_client_t *client,const char *credential_json);
esp_err_t qqmusic_client_restore(qqmusic_client_t *client);
esp_err_t qqmusic_client_verify(qqmusic_client_t *client);
esp_err_t qqmusic_client_refresh(qqmusic_client_t *client);
esp_err_t qqmusic_client_logout(qqmusic_client_t *client);
esp_err_t qqmusic_client_playlists(qqmusic_client_t *client,qqmusic_playlists_t *out);
esp_err_t qqmusic_client_tracks(qqmusic_client_t *client,const qqmusic_playlist_t *playlist,uint32_t offset,qqmusic_tracks_t *out);
esp_err_t qqmusic_client_audio(qqmusic_client_t *client,const qqmusic_track_t *track,qqmusic_audio_t *out);
