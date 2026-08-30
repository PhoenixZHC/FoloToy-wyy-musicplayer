#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) || !defined(NETEASE_CLIENT_PARSER_ONLY)
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 0x101
#define ESP_ERR_NO_MEM 0x102
#define ESP_ERR_INVALID_ARG 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_RESPONSE 0x105
#define ESP_ERR_NOT_FOUND 0x106
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NETEASE_CLIENT_BASE_URL_LEN 128
#define NETEASE_CLIENT_DEVICE_ID_LEN 64
#define NETEASE_CLIENT_DEVICE_SECRET_LEN 128
#define NETEASE_CLIENT_LOGIN_ID_LEN 64
#define NETEASE_CLIENT_QR_PAYLOAD_LEN 256
#define NETEASE_CLIENT_NAME_LEN 64
#define NETEASE_CLIENT_OWNER_LEN 64
#define NETEASE_CLIENT_ARTIST_LEN 96
#define NETEASE_CLIENT_FORMAT_LEN 16
#define NETEASE_CLIENT_TRACK_URL_LEN 512
#define NETEASE_CLIENT_MAX_PLAYLISTS 20
#define NETEASE_CLIENT_MAX_TRACKS 15
#define NETEASE_CLIENT_RESPONSE_MAX_BYTES 4096

typedef struct {
    char base_url[NETEASE_CLIENT_BASE_URL_LEN];
    char device_id[NETEASE_CLIENT_DEVICE_ID_LEN];
    char device_secret[NETEASE_CLIENT_DEVICE_SECRET_LEN];
    int timeout_ms;
} netease_client_t;

typedef struct {
    const char *base_url;
    const char *device_id;
    const char *device_secret;
    int timeout_ms;
} netease_client_config_t;

typedef struct {
    char login_id[NETEASE_CLIENT_LOGIN_ID_LEN];
    char qr_payload[NETEASE_CLIENT_QR_PAYLOAD_LEN];
    int expires_in_seconds;
} netease_qr_start_t;

typedef enum {
    NETEASE_QR_WAITING = 0,
    NETEASE_QR_SCANNED,
    NETEASE_QR_CONFIRMED,
    NETEASE_QR_EXPIRED,
    NETEASE_QR_FAILED,
} netease_qr_state_t;

typedef struct {
    netease_qr_state_t state;
    int64_t netease_user_id;
    char nickname[NETEASE_CLIENT_NAME_LEN];
    char message[NETEASE_CLIENT_NAME_LEN];
} netease_qr_status_t;

typedef struct {
    int64_t id;
    char name[NETEASE_CLIENT_NAME_LEN];
    int track_count;
    char owner_name[NETEASE_CLIENT_OWNER_LEN];
} netease_playlist_t;

typedef struct {
    size_t count;
    netease_playlist_t items[NETEASE_CLIENT_MAX_PLAYLISTS];
} netease_playlist_list_t;

typedef struct {
    int64_t id;
    char name[NETEASE_CLIENT_NAME_LEN];
    char artist[NETEASE_CLIENT_ARTIST_LEN];
    int duration_ms;
} netease_track_t;

typedef struct {
    int64_t playlist_id;
    size_t count;
    int next_offset;
    bool has_next_offset;
    netease_track_t items[NETEASE_CLIENT_MAX_TRACKS];
} netease_track_list_t;

typedef struct {
    int64_t track_id;
    char url[NETEASE_CLIENT_TRACK_URL_LEN];
    char format[NETEASE_CLIENT_FORMAT_LEN];
    int expires_in_seconds;
} netease_track_url_t;

esp_err_t netease_client_init(netease_client_t *client, const netease_client_config_t *config);
esp_err_t netease_client_register_device(const netease_client_t *client,
                                         const char *device_name,
                                         const char *registration_code);
esp_err_t netease_client_hello(const netease_client_t *client);
esp_err_t netease_client_qr_start(const netease_client_t *client, netease_qr_start_t *out);
esp_err_t netease_client_qr_status(const netease_client_t *client,
                                   const char *login_id,
                                   netease_qr_status_t *out);
esp_err_t netease_client_logout(const netease_client_t *client);
esp_err_t netease_client_get_playlists(const netease_client_t *client, netease_playlist_list_t *out);
esp_err_t netease_client_get_tracks(const netease_client_t *client,
                                    int64_t playlist_id,
                                    uint32_t limit,
                                    uint32_t offset,
                                    netease_track_list_t *out);
esp_err_t netease_client_get_track_url(const netease_client_t *client,
                                       int64_t track_id,
                                       int bitrate,
                                       netease_track_url_t *out);

esp_err_t netease_parse_qr_start(const char *json, netease_qr_start_t *out);
esp_err_t netease_parse_qr_status(const char *json, netease_qr_status_t *out);
esp_err_t netease_parse_playlists(const char *json, netease_playlist_list_t *out);
esp_err_t netease_parse_tracks(const char *json, netease_track_list_t *out);
esp_err_t netease_parse_track_url(const char *json, netease_track_url_t *out);

#ifdef __cplusplus
}
#endif
