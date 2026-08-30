#include "netease_client.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#ifndef NETEASE_CLIENT_PARSER_ONLY
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "mbedtls/md.h"
#endif

#define NETEASE_CLIENT_DEFAULT_TIMEOUT_MS 10000
#define NETEASE_CLIENT_SIGNATURE_HEX_LEN 64
#define NETEASE_CLIENT_BODY_HASH_HEX_LEN 64

static esp_err_t copy_text(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0 || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t src_len = strlen(src);
    if (src_len >= dst_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(dst, src, src_len + 1);
    return ESP_OK;
}

static esp_err_t copy_json_string(cJSON *object, const char *key, char *dst, size_t dst_len, bool required)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        if (required) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        dst[0] = '\0';
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return copy_text(dst, dst_len, item->valuestring);
}

static esp_err_t get_json_int(cJSON *object, const char *key, int *out, bool required)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        if (required) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *out = 0;
        return ESP_OK;
    }
    if (!cJSON_IsNumber(item) ||
        item->valuedouble < (double)INT_MIN ||
        item->valuedouble > (double)INT_MAX ||
        item->valuedouble != (double)item->valueint) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = item->valueint;
    return ESP_OK;
}

static esp_err_t get_json_i64(cJSON *object, const char *key, int64_t *out, bool required)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        if (required) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        *out = 0;
        return ESP_OK;
    }
    if (!cJSON_IsNumber(item) ||
        item->valuedouble < (double)INT64_MIN ||
        item->valuedouble > (double)INT64_MAX ||
        item->valuedouble != (double)(int64_t)item->valuedouble) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = (int64_t)item->valuedouble;
    return ESP_OK;
}

static esp_err_t require_positive_i64(int64_t value)
{
    return value > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t require_non_negative_int(int value)
{
    return value >= 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static cJSON *parse_root(const char *json)
{
    if (json == NULL || json[0] == '\0' ||
        strlen(json) > NETEASE_CLIENT_RESPONSE_MAX_BYTES) {
        return NULL;
    }
    return cJSON_Parse(json);
}

esp_err_t netease_parse_qr_start(const char *json, netease_qr_start_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = parse_root(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = copy_json_string(root, "login_id", out->login_id, sizeof(out->login_id), true);
    if (err == ESP_OK) {
        err = copy_json_string(root, "qr_payload", out->qr_payload, sizeof(out->qr_payload), true);
    }
    if (err == ESP_OK) {
        err = get_json_int(root, "expires_in_seconds", &out->expires_in_seconds, true);
        if (err == ESP_OK && out->expires_in_seconds <= 0) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t netease_parse_qr_status(const char *json, netease_qr_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = parse_root(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    char state[16] = {0};
    esp_err_t err = copy_json_string(root, "state", state, sizeof(state), true);
    if (err == ESP_OK) {
        if (strcmp(state, "waiting") == 0) {
            out->state = NETEASE_QR_WAITING;
        } else if (strcmp(state, "scanned") == 0) {
            out->state = NETEASE_QR_SCANNED;
        } else if (strcmp(state, "confirmed") == 0) {
            out->state = NETEASE_QR_CONFIRMED;
            err = get_json_i64(root, "netease_user_id", &out->netease_user_id, false);
            if (err == ESP_OK) {
                err = copy_json_string(root, "nickname", out->nickname, sizeof(out->nickname), false);
            }
        } else if (strcmp(state, "expired") == 0) {
            out->state = NETEASE_QR_EXPIRED;
        } else if (strcmp(state, "failed") == 0) {
            out->state = NETEASE_QR_FAILED;
            err = copy_json_string(root, "message", out->message, sizeof(out->message), false);
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t netease_parse_playlists(const char *json, netease_playlist_list_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = parse_root(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = ESP_OK;
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "playlists");
    if (!cJSON_IsArray(items)) {
        err = ESP_ERR_INVALID_RESPONSE;
    } else {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, items) {
            if (out->count >= NETEASE_CLIENT_MAX_PLAYLISTS) {
                break;
            }
            netease_playlist_t *playlist = &out->items[out->count];
            err = get_json_i64(item, "id", &playlist->id, true);
            if (err == ESP_OK) {
                err = require_positive_i64(playlist->id);
            }
            if (err == ESP_OK) {
                err = copy_json_string(item, "name", playlist->name, sizeof(playlist->name), true);
            }
            if (err == ESP_OK) {
                err = get_json_int(item, "track_count", &playlist->track_count, true);
                if (err == ESP_OK) {
                    err = require_non_negative_int(playlist->track_count);
                }
            }
            if (err == ESP_OK) {
                err = copy_json_string(item, "owner_name", playlist->owner_name, sizeof(playlist->owner_name), true);
            }
            if (err != ESP_OK) {
                break;
            }
            out->count++;
        }
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t netease_parse_tracks(const char *json, netease_track_list_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = parse_root(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = get_json_i64(root, "playlist_id", &out->playlist_id, true);
    if (err == ESP_OK) {
        err = require_positive_i64(out->playlist_id);
    }
    cJSON *tracks = cJSON_GetObjectItemCaseSensitive(root, "tracks");
    if (err == ESP_OK && !cJSON_IsArray(tracks)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    if (err == ESP_OK) {
        cJSON *next_offset = cJSON_GetObjectItemCaseSensitive(root, "next_offset");
        if (cJSON_IsNumber(next_offset)) {
            int parsed = 0;
            err = get_json_int(root, "next_offset", &parsed, false);
            if (err == ESP_OK && parsed >= 0) {
                out->next_offset = parsed;
                out->has_next_offset = true;
            } else if (err == ESP_OK) {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }

    if (err == ESP_OK) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, tracks) {
            if (out->count >= NETEASE_CLIENT_MAX_TRACKS) {
                break;
            }
            netease_track_t *track = &out->items[out->count];
            err = get_json_i64(item, "id", &track->id, true);
            if (err == ESP_OK) {
                err = require_positive_i64(track->id);
            }
            if (err == ESP_OK) {
                err = copy_json_string(item, "name", track->name, sizeof(track->name), true);
            }
            if (err == ESP_OK) {
                err = copy_json_string(item, "artist", track->artist, sizeof(track->artist), true);
            }
            if (err == ESP_OK) {
                err = get_json_int(item, "duration_ms", &track->duration_ms, true);
                if (err == ESP_OK) {
                    err = require_non_negative_int(track->duration_ms);
                }
            }
            if (err != ESP_OK) {
                break;
            }
            out->count++;
        }
    }

    cJSON_Delete(root);
    return err;
}

esp_err_t netease_parse_track_url(const char *json, netease_track_url_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = parse_root(json);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    char error[32] = {0};
    if (copy_json_string(root, "error", error, sizeof(error), false) == ESP_OK &&
        strcmp(error, "track_unavailable") == 0) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = get_json_i64(root, "track_id", &out->track_id, true);
    if (err == ESP_OK) {
        err = require_positive_i64(out->track_id);
    }
    if (err == ESP_OK) {
        err = copy_json_string(root, "url", out->url, sizeof(out->url), true);
    }
    if (err == ESP_OK) {
        err = copy_json_string(root, "format", out->format, sizeof(out->format), true);
    }
    if (err == ESP_OK) {
        err = get_json_int(root, "expires_in_seconds", &out->expires_in_seconds, true);
        if (err == ESP_OK && out->expires_in_seconds < 0) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    cJSON_Delete(root);
    return err;
}

#ifndef NETEASE_CLIENT_PARSER_ONLY
typedef struct {
    char *buffer;
    size_t cap;
    size_t len;
    bool overflow;
} response_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    response_buffer_t *response = (response_buffer_t *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data == NULL) {
        return ESP_OK;
    }

    if (response->len + event->data_len >= response->cap) {
        response->overflow = true;
        return ESP_FAIL;
    }

    memcpy(response->buffer + response->len, event->data, event->data_len);
    response->len += event->data_len;
    response->buffer[response->len] = '\0';
    return ESP_OK;
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static esp_err_t sha256_hex(const char *text, char out[NETEASE_CLIENT_BODY_HASH_HEX_LEN + 1])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_FAIL;
    }

    uint8_t hash[32] = {0};
    const unsigned char *data = (const unsigned char *)(text == NULL ? "" : text);
    size_t len = strlen((const char *)data);
    int rc = mbedtls_md(info, data, len, hash);
    if (rc != 0) {
        return ESP_FAIL;
    }

    bytes_to_hex(hash, sizeof(hash), out);
    return ESP_OK;
}

static esp_err_t hmac_sha256_hex(const char *secret,
                                 const char *text,
                                 char out[NETEASE_CLIENT_SIGNATURE_HEX_LEN + 1])
{
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return ESP_FAIL;
    }

    uint8_t digest[32] = {0};
    int rc = mbedtls_md_hmac(info,
                             (const unsigned char *)secret,
                             strlen(secret),
                             (const unsigned char *)text,
                             strlen(text),
                             digest);
    if (rc != 0) {
        return ESP_FAIL;
    }

    bytes_to_hex(digest, sizeof(digest), out);
    return ESP_OK;
}

static esp_err_t append_url(const netease_client_t *client, const char *path, char *out, size_t out_len)
{
    int written = snprintf(out, out_len, "%s%s", client->base_url, path);
    if (written < 0 || (size_t)written >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t build_signature_headers(const netease_client_t *client,
                                         const char *method,
                                         const char *path,
                                         const char *body,
                                         char *authorization,
                                         size_t authorization_len,
                                         char *timestamp,
                                         size_t timestamp_len)
{
    time_t now = time(NULL);
    if (now <= 0) {
        now = 1;
    }

    int ts_written = snprintf(timestamp, timestamp_len, "%lld", (long long)now);
    if (ts_written < 0 || (size_t)ts_written >= timestamp_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    char body_hash[NETEASE_CLIENT_BODY_HASH_HEX_LEN + 1] = {0};
    esp_err_t err = sha256_hex(body, body_hash);
    if (err != ESP_OK) {
        return err;
    }

    char signature_input[256] = {0};
    int input_written = snprintf(signature_input,
                                 sizeof(signature_input),
                                 "%s\n%s\n%s\n%s",
                                 method,
                                 path,
                                 body_hash,
                                 timestamp);
    if (input_written < 0 || (size_t)input_written >= sizeof(signature_input)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char signature[NETEASE_CLIENT_SIGNATURE_HEX_LEN + 1] = {0};
    err = hmac_sha256_hex(client->device_secret, signature_input, signature);
    if (err != ESP_OK) {
        return err;
    }

    int auth_written = snprintf(authorization,
                                authorization_len,
                                "Device %s:%s",
                                client->device_id,
                                signature);
    if (auth_written < 0 || (size_t)auth_written >= authorization_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t request_json(const netease_client_t *client,
                              esp_http_client_method_t method,
                              const char *method_name,
                              const char *path,
                              const char *body,
                              bool signed_request,
                              char *response,
                              size_t response_len)
{
    if (client == NULL || path == NULL || response == NULL || response_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[NETEASE_CLIENT_BASE_URL_LEN + 128] = {0};
    esp_err_t err = append_url(client, path, url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    response_buffer_t buffer = {
        .buffer = response,
        .cap = response_len,
    };
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = client->timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &buffer,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t http = esp_http_client_init(&http_config);
    if (http == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_http_client_set_method(http, method);
    if (err == ESP_OK) {
        err = esp_http_client_set_header(http, "Accept", "application/json");
    }
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_header(http, "Content-Type", "application/json");
    }
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_post_field(http, body, strlen(body));
    }

    char authorization[NETEASE_CLIENT_DEVICE_ID_LEN + NETEASE_CLIENT_SIGNATURE_HEX_LEN + 16] = {0};
    char timestamp[24] = {0};
    if (err == ESP_OK && signed_request) {
        err = build_signature_headers(client,
                                      method_name,
                                      path,
                                      body,
                                      authorization,
                                      sizeof(authorization),
                                      timestamp,
                                      sizeof(timestamp));
        if (err == ESP_OK) {
            err = esp_http_client_set_header(http, "Authorization", authorization);
        }
        if (err == ESP_OK) {
            err = esp_http_client_set_header(http, "X-Device-Timestamp", timestamp);
        }
    }

    if (err == ESP_OK) {
        err = esp_http_client_perform(http);
    }
    int status_code = esp_http_client_get_status_code(http);
    esp_http_client_cleanup(http);

    if (err != ESP_OK) {
        return err;
    }
    if (buffer.overflow) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (status_code < 200 || status_code >= 300) {
        if (status_code == 404) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t request_alloc(const netease_client_t *client,
                               esp_http_client_method_t method,
                               const char *method_name,
                               const char *path,
                               const char *body,
                               bool signed_request,
                               char **response)
{
    char *buffer = calloc(1, NETEASE_CLIENT_RESPONSE_MAX_BYTES + 1);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = request_json(client,
                                 method,
                                 method_name,
                                 path,
                                 body,
                                 signed_request,
                                 buffer,
                                 NETEASE_CLIENT_RESPONSE_MAX_BYTES + 1);
    if (err != ESP_OK) {
        free(buffer);
        return err;
    }

    *response = buffer;
    return ESP_OK;
}

esp_err_t netease_client_init(netease_client_t *client, const netease_client_config_t *config)
{
    if (client == NULL || config == NULL || config->base_url == NULL ||
        config->device_id == NULL || config->device_secret == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(client, 0, sizeof(*client));
    esp_err_t err = copy_text(client->base_url, sizeof(client->base_url), config->base_url);
    if (err == ESP_OK) {
        err = copy_text(client->device_id, sizeof(client->device_id), config->device_id);
    }
    if (err == ESP_OK) {
        err = copy_text(client->device_secret, sizeof(client->device_secret), config->device_secret);
    }
    if (err != ESP_OK) {
        memset(client, 0, sizeof(*client));
        return err;
    }

    client->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : NETEASE_CLIENT_DEFAULT_TIMEOUT_MS;
    return ESP_OK;
}

esp_err_t netease_client_register_device(const netease_client_t *client,
                                         const char *device_name,
                                         const char *registration_code)
{
    if (client == NULL || device_name == NULL || registration_code == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    bool ok = cJSON_AddStringToObject(root, "device_id", client->device_id) != NULL &&
              cJSON_AddStringToObject(root, "device_name", device_name) != NULL &&
              cJSON_AddStringToObject(root, "device_secret", client->device_secret) != NULL &&
              cJSON_AddStringToObject(root, "registration_code", registration_code) != NULL;
    if (!ok) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char response[128] = {0};
    esp_err_t err = request_json(client,
                                 HTTP_METHOD_POST,
                                 "POST",
                                 "/api/v1/device/register",
                                 body,
                                 false,
                                 response,
                                 sizeof(response));
    cJSON_free(body);
    return err;
}

esp_err_t netease_client_hello(const netease_client_t *client)
{
    char response[128] = {0};
    return request_json(client,
                        HTTP_METHOD_POST,
                        "POST",
                        "/api/v1/device/hello",
                        "{}",
                        true,
                        response,
                        sizeof(response));
}

esp_err_t netease_client_qr_start(const netease_client_t *client, netease_qr_start_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *response = NULL;
    esp_err_t err = request_alloc(client,
                                  HTTP_METHOD_POST,
                                  "POST",
                                  "/api/v1/auth/qr/start",
                                  "{}",
                                  true,
                                  &response);
    if (err == ESP_OK) {
        err = netease_parse_qr_start(response, out);
    }
    free(response);
    return err;
}

esp_err_t netease_client_qr_status(const netease_client_t *client,
                                   const char *login_id,
                                   netease_qr_status_t *out)
{
    if (login_id == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[128] = {0};
    int written = snprintf(path, sizeof(path), "/api/v1/auth/qr/status?login_id=%s", login_id);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *response = NULL;
    esp_err_t err = request_alloc(client, HTTP_METHOD_GET, "GET", path, NULL, true, &response);
    if (err == ESP_OK) {
        err = netease_parse_qr_status(response, out);
    }
    free(response);
    return err;
}

esp_err_t netease_client_logout(const netease_client_t *client)
{
    char response[128] = {0};
    return request_json(client,
                        HTTP_METHOD_POST,
                        "POST",
                        "/api/v1/auth/logout",
                        "{}",
                        true,
                        response,
                        sizeof(response));
}

esp_err_t netease_client_get_playlists(const netease_client_t *client, netease_playlist_list_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *response = NULL;
    esp_err_t err = request_alloc(client,
                                  HTTP_METHOD_GET,
                                  "GET",
                                  "/api/v1/me/playlists",
                                  NULL,
                                  true,
                                  &response);
    if (err == ESP_OK) {
        err = netease_parse_playlists(response, out);
    }
    free(response);
    return err;
}

esp_err_t netease_client_get_tracks(const netease_client_t *client,
                                    int64_t playlist_id,
                                    uint32_t limit,
                                    uint32_t offset,
                                    netease_track_list_t *out)
{
    if (out == NULL || playlist_id <= 0 || limit == 0 || limit > NETEASE_CLIENT_MAX_TRACKS) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[160] = {0};
    int written = snprintf(path,
                           sizeof(path),
                           "/api/v1/playlists/%" PRId64 "/tracks?limit=%" PRIu32 "&offset=%" PRIu32,
                           playlist_id,
                           limit,
                           offset);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *response = NULL;
    esp_err_t err = request_alloc(client, HTTP_METHOD_GET, "GET", path, NULL, true, &response);
    if (err == ESP_OK) {
        err = netease_parse_tracks(response, out);
    }
    free(response);
    return err;
}

esp_err_t netease_client_get_track_url(const netease_client_t *client,
                                       int64_t track_id,
                                       int bitrate,
                                       netease_track_url_t *out)
{
    if (out == NULL || track_id <= 0 || bitrate < 16000 || bitrate > 320000) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[80] = {0};
    int path_written = snprintf(path, sizeof(path), "/api/v1/tracks/%" PRId64 "/url", track_id);
    if (path_written < 0 || (size_t)path_written >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char body[32] = {0};
    int body_written = snprintf(body, sizeof(body), "{\"bitrate\":%d}", bitrate);
    if (body_written < 0 || (size_t)body_written >= sizeof(body)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *response = NULL;
    esp_err_t err = request_alloc(client, HTTP_METHOD_POST, "POST", path, body, true, &response);
    if (err == ESP_OK) {
        err = netease_parse_track_url(response, out);
    }
    free(response);
    return err;
}
#endif
