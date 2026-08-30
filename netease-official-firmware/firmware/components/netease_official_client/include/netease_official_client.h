#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) || !defined(NETEASE_OFFICIAL_HOST_TEST)
#include "esp_err.h"
#include "esp_http_client.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 0x101
#define ESP_ERR_NO_MEM 0x102
#define ESP_ERR_INVALID_ARG 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_RESPONSE 0x105
#define ESP_ERR_NOT_FOUND 0x106
#define ESP_ERR_INVALID_STATE 0x107
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST = 1,
} esp_http_client_method_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NETEASE_OFFICIAL_HOST "https://openapi.music.163.com"
#define NETEASE_OFFICIAL_SIGN_TYPE "RSA_SHA256"
#define NETEASE_OFFICIAL_CHANNEL "iotapitest"
#define NETEASE_OFFICIAL_DEVICE_TYPE "openapi"
#define NETEASE_OFFICIAL_OS "openapi"
#define NETEASE_OFFICIAL_OS_VERSION "14"
#define NETEASE_OFFICIAL_BRAND "iotapitest"
#define NETEASE_OFFICIAL_MODEL "GDI-W09"
#define NETEASE_OFFICIAL_CLIENT_IP "127.0.0.1"
#define NETEASE_OFFICIAL_NET_STATUS "4g"

#define NETEASE_OFFICIAL_PATH_ANONYMOUS_LOGIN "/openapi/music/basic/oauth2/login/anonymous"
#define NETEASE_OFFICIAL_PATH_QR_START "/openapi/music/basic/user/oauth2/qrcodekey/get/v2"
#define NETEASE_OFFICIAL_PATH_QR_POLL "/openapi/music/basic/oauth2/device/login/qrcode/get"
#define NETEASE_OFFICIAL_PATH_TOKEN_REFRESH "/openapi/music/basic/user/oauth2/token/refresh/v2"
#define NETEASE_OFFICIAL_PATH_PLAYLIST_CREATED "/openapi/music/basic/playlist/created/get/v2"
#define NETEASE_OFFICIAL_PATH_PLAYLIST_SUBSCRIBED "/openapi/music/basic/playlist/subed/get/v2"
#define NETEASE_OFFICIAL_PATH_PLAYLIST_TRACKS "/openapi/music/basic/playlist/song/list/get/v5"
#define NETEASE_OFFICIAL_PATH_SONG_DETAIL "/openapi/music/basic/song/detail/get/v2"
#define NETEASE_OFFICIAL_PATH_PLAY_URL "/openapi/music/basic/song/playurl/get/v2"

#define NETEASE_OFFICIAL_TOKEN_LEN 768
#define NETEASE_OFFICIAL_QR_KEY_LEN 128
#define NETEASE_OFFICIAL_QR_URL_LEN 384
#define NETEASE_OFFICIAL_NAME_LEN 64
#define NETEASE_OFFICIAL_OWNER_LEN 64
#define NETEASE_OFFICIAL_ARTIST_LEN 96
#define NETEASE_OFFICIAL_FORMAT_LEN 16
#define NETEASE_OFFICIAL_PLAY_URL_LEN 512
#define NETEASE_OFFICIAL_ID_LEN 64
#define NETEASE_OFFICIAL_MAX_PLAYLISTS 20
#define NETEASE_OFFICIAL_MAX_TRACKS 30
#define NETEASE_OFFICIAL_RESPONSE_MAX_BYTES 4096

typedef struct {
    const char *app_id;
    const char *app_secret;
    const char *private_key_pem;
    const char *device_id;
    const char *app_version;
    int timeout_ms;
} netease_official_config_t;

typedef struct netease_official_client_t netease_official_client_t;

typedef esp_err_t (*netease_official_transport_fn)(const netease_official_client_t *client,
                                                   const char *path,
                                                   esp_http_client_method_t method,
                                                   const char *encoded_query,
                                                   char *response,
                                                   size_t response_size,
                                                   int *http_status);

struct netease_official_client_t {
    netease_official_config_t config;
#ifdef NETEASE_OFFICIAL_HOST_TEST
    netease_official_transport_fn test_transport;
#endif
};

typedef struct {
    char access_token[NETEASE_OFFICIAL_TOKEN_LEN];
    char refresh_token[NETEASE_OFFICIAL_TOKEN_LEN];
    int64_t access_expires_at_ms;
    int64_t refresh_expires_at_ms;
} netease_token_set_t;

typedef enum {
    NETEASE_OFFICIAL_QR_EXPIRED = 800,
    NETEASE_OFFICIAL_QR_WAITING = 801,
    NETEASE_OFFICIAL_QR_SCANNED = 802,
    NETEASE_OFFICIAL_QR_CONFIRMED = 803,
    NETEASE_OFFICIAL_QR_FAILED = 804,
} netease_official_qr_state_t;

typedef struct {
    char unikey[NETEASE_OFFICIAL_QR_KEY_LEN];
    char qr_code_url[NETEASE_OFFICIAL_QR_URL_LEN];
    int expires_in_seconds;
} netease_official_qr_t;

typedef struct {
    netease_official_qr_state_t state;
    netease_token_set_t tokens;
    char message[96];
} netease_official_qr_status_t;

typedef struct {
    char id[NETEASE_OFFICIAL_ID_LEN];
    char name[NETEASE_OFFICIAL_NAME_LEN];
    int track_count;
    char owner_name[NETEASE_OFFICIAL_OWNER_LEN];
} netease_official_playlist_t;

typedef struct {
    size_t count;
    bool has_more;
    netease_official_playlist_t items[NETEASE_OFFICIAL_MAX_PLAYLISTS];
} netease_official_playlist_list_t;

typedef struct {
    char id[NETEASE_OFFICIAL_ID_LEN];
    char name[NETEASE_OFFICIAL_NAME_LEN];
    char artist[NETEASE_OFFICIAL_ARTIST_LEN];
    int duration_ms;
} netease_official_track_t;

typedef struct {
    char playlist_id[NETEASE_OFFICIAL_ID_LEN];
    size_t count;
    uint32_t next_offset;
    bool has_more;
    netease_official_track_t items[NETEASE_OFFICIAL_MAX_TRACKS];
} netease_official_track_list_t;

typedef struct {
    char song_id[NETEASE_OFFICIAL_ID_LEN];
    char url[NETEASE_OFFICIAL_PLAY_URL_LEN];
    char format[NETEASE_OFFICIAL_FORMAT_LEN];
    int expires_in_seconds;
} netease_official_play_url_t;

typedef struct {
    const char *key;
    const char *value;
} official_param_t;

typedef struct cJSON cJSON;

typedef struct {
    int http_status;
    int code;
    char sub_code[48];
    char message[128];
    cJSON *root;
    cJSON *data;
} official_api_result_t;

esp_err_t official_url_encode(const char *input, char *output, size_t output_size);
esp_err_t official_build_device_json(const char *device_id,
                                     const char *app_version,
                                     char *output,
                                     size_t output_size);
esp_err_t official_build_canonical(const official_param_t *params,
                                   size_t count,
                                   char *output,
                                   size_t output_size);
esp_err_t official_parse_envelope(char *response,
                                  int http_status,
                                  official_api_result_t *out);
void official_api_result_clear(official_api_result_t *result);
esp_err_t official_sign_rsa_sha256_base64(const char *private_key_pem,
                                          const char *canonical,
                                          char *signature,
                                          size_t signature_size);
esp_err_t official_http_request(const netease_official_client_t *client,
                                const char *path,
                                esp_http_client_method_t method,
                                const char *encoded_query,
                                char *response,
                                size_t response_size,
                                int *http_status);
#ifdef NETEASE_OFFICIAL_HOST_TEST
void netease_official_set_test_transport(netease_official_client_t *client,
                                         netease_official_transport_fn transport);
#endif

esp_err_t netease_official_init(netease_official_client_t *client,
                                const netease_official_config_t *config);
esp_err_t netease_official_anonymous_login(netease_official_client_t *client,
                                           char *token,
                                           size_t token_size);
esp_err_t netease_official_qr_start(netease_official_client_t *client,
                                    const char *anonymous_token,
                                    netease_official_qr_t *out);
esp_err_t netease_official_qr_poll(netease_official_client_t *client,
                                   const char *anonymous_token,
                                   const char *unikey,
                                   netease_official_qr_status_t *out);
esp_err_t netease_official_refresh(netease_official_client_t *client,
                                   const char *refresh_token,
                                   netease_token_set_t *out);
esp_err_t netease_official_get_playlists(netease_official_client_t *client,
                                         const char *access_token,
                                         netease_official_playlist_list_t *out);
esp_err_t netease_official_get_tracks(netease_official_client_t *client,
                                      const char *access_token,
                                      const char *playlist_id,
                                      uint32_t limit,
                                      uint32_t offset,
                                      netease_official_track_list_t *out);
esp_err_t netease_official_get_play_url(netease_official_client_t *client,
                                        const char *access_token,
                                        const char *song_id,
                                        int bitrate,
                                        netease_official_play_url_t *out);

#ifdef __cplusplus
}
#endif
