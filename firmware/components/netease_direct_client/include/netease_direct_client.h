#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) || !defined(NETEASE_DIRECT_HOST_TEST)
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 0x101
#define ESP_ERR_NO_MEM 0x102
#define ESP_ERR_INVALID_ARG 0x103
#define ESP_ERR_INVALID_RESPONSE 0x105
#define ESP_ERR_NOT_FOUND 0x106
#define ESP_ERR_INVALID_STATE 0x107
#define ESP_ERR_INVALID_SIZE 0x104
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NETEASE_DIRECT_COOKIE_MAX_BYTES 3072
#define NETEASE_DIRECT_QR_KEY_MAX 128
#define NETEASE_DIRECT_QR_URL_MAX 384
#define NETEASE_DIRECT_NAME_MAX 64
#define NETEASE_DIRECT_ARTIST_MAX 96
#define NETEASE_DIRECT_URL_MAX 512
#define NETEASE_DIRECT_MAX_PLAYLISTS 20
#define NETEASE_DIRECT_MAX_TRACKS 8
/* Only an explicit authentication rejection permits removing a saved session. */
#define NETEASE_DIRECT_ERR_AUTH ((esp_err_t)0x7e01)
#define NETEASE_DIRECT_ERR_LOGIN_REJECTED ((esp_err_t)0x7e02)
#define NETEASE_DIRECT_ERR_UNAVAILABLE ((esp_err_t)0x7e03)

typedef enum {
    NETEASE_DIRECT_QR_WAITING = 801,
    NETEASE_DIRECT_QR_SCANNED = 802,
    NETEASE_DIRECT_QR_CONFIRMED = 803,
    NETEASE_DIRECT_QR_EXPIRED = 800,
    NETEASE_DIRECT_QR_FAILED = 0,
} netease_direct_qr_state_t;

typedef struct {
    char key[NETEASE_DIRECT_QR_KEY_MAX];
    char payload[NETEASE_DIRECT_QR_URL_MAX];
    int expires_in_seconds;
} netease_direct_qr_t;

typedef struct {
    netease_direct_qr_state_t state;
    char nickname[NETEASE_DIRECT_NAME_MAX];
    char message[NETEASE_DIRECT_NAME_MAX];
} netease_direct_qr_status_t;

typedef struct {
    int64_t id;
    char name[NETEASE_DIRECT_NAME_MAX];
    int track_count;
} netease_direct_playlist_t;

typedef struct {
    size_t count;
    bool truncated;
    netease_direct_playlist_t items[NETEASE_DIRECT_MAX_PLAYLISTS];
} netease_direct_playlist_list_t;

typedef struct {
    int64_t id;
    char name[NETEASE_DIRECT_NAME_MAX];
    char artist[NETEASE_DIRECT_ARTIST_MAX];
    int duration_ms;
} netease_direct_track_t;

typedef struct {
    int64_t playlist_id;
    size_t count;
    uint32_t next_offset;
    bool has_next_offset;
    netease_direct_track_t items[NETEASE_DIRECT_MAX_TRACKS];
} netease_direct_track_list_t;

typedef struct {
    int64_t track_id;
    char url[NETEASE_DIRECT_URL_MAX];
    char format[16];
    int expires_in_seconds;
} netease_direct_track_url_t;

typedef struct {
    char cookie[NETEASE_DIRECT_COOKIE_MAX_BYTES + 1];
    int64_t user_id;
    char nickname[NETEASE_DIRECT_NAME_MAX];
} netease_direct_session_t;

typedef struct {
    int timeout_ms;
} netease_direct_client_config_t;

esp_err_t netease_direct_parse_qr_key(const char *json, netease_direct_qr_t *out);
esp_err_t netease_direct_parse_qr_status(const char *json, netease_direct_qr_status_t *out);
esp_err_t netease_direct_parse_qr_user_id(const char *json, int64_t *out);
esp_err_t netease_direct_parse_playlists(const char *json, netease_direct_playlist_list_t *out);
esp_err_t netease_direct_parse_tracks(const char *json, int64_t playlist_id,
                                      netease_direct_track_list_t *out);
esp_err_t netease_direct_parse_track_url(const char *json, netease_direct_track_url_t *out);

esp_err_t netease_direct_build_weapi_body(const char *json, char *out, size_t out_size);
esp_err_t netease_direct_build_weapi_body_with_key(const char *json,
                                                    const unsigned char secret[16],
                                                    char *out, size_t out_size);
esp_err_t netease_direct_build_eapi_body(const char *path, const char *json,
                                          char *out, size_t out_size);
/* Caller frees the returned JSON with free(). Cookie values are never logged. */
char *netease_direct_session_json(const char *json, const char *cookie, bool eapi);

esp_err_t netease_direct_cookie_copy(const char *cookie, char *out, size_t out_size);
bool netease_direct_cookie_has_login(const char *cookie);
esp_err_t netease_direct_cookie_merge(char *cookie, size_t capacity, const char *set_cookie);

#if defined(ESP_PLATFORM)
/* Single-worker API: not reentrant. Business calls use the loaded local session. */
esp_err_t netease_direct_client_init(const netease_direct_client_config_t *config);
esp_err_t netease_direct_session_load(netease_direct_session_t *out);
esp_err_t netease_direct_session_save(const char *cookie, int64_t user_id, const char *nickname);
esp_err_t netease_direct_session_clear(void);
esp_err_t netease_direct_session_verify(void);
esp_err_t netease_direct_qr_start(netease_direct_qr_t *out);
esp_err_t netease_direct_qr_poll(const char *key, netease_direct_qr_status_t *out);
esp_err_t netease_direct_get_playlists(const char *cookie, netease_direct_playlist_list_t *out);
esp_err_t netease_direct_get_tracks(const char *cookie, int64_t playlist_id,
                                    uint32_t limit, uint32_t offset,
                                    netease_direct_track_list_t *out);
esp_err_t netease_direct_get_track_url(const char *cookie, int64_t track_id,
                                       int bitrate, netease_direct_track_url_t *out);
/* Optional media failures never invalidate a working audio session. */
esp_err_t netease_direct_get_cover_url(int64_t track_id, char *out, size_t capacity);
esp_err_t netease_direct_get_lyrics(int64_t track_id, char *out, size_t capacity);
#endif

#ifdef __cplusplus
}
#endif
