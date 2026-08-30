#include "netease_official_client.h"

#include "cJSON.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include <sys/time.h>
#endif

#define NETEASE_OFFICIAL_DEFAULT_TIMEOUT_MS 10000
#define NETEASE_OFFICIAL_CANONICAL_LEN 1536
#define NETEASE_OFFICIAL_QUERY_LEN 4096
#define NETEASE_OFFICIAL_BIZ_LEN 512
#define NETEASE_OFFICIAL_DEVICE_LEN 256
#define NETEASE_OFFICIAL_SIGNATURE_LEN 1024
#define NETEASE_OFFICIAL_ENCODED_VALUE_LEN 2304
#define NETEASE_OFFICIAL_REFRESH_SECONDS (20 * 24 * 60 * 60)
#define NETEASE_OFFICIAL_PLAY_URL_SECONDS (25 * 60)

static bool is_empty_or_placeholder(const char *value)
{
    return value == NULL || value[0] == '\0' || strcmp(value, "replace-me") == 0;
}

static bool has_private_key_markers(const char *pem)
{
    return pem != NULL &&
           strstr(pem, "-----BEGIN PRIVATE KEY-----") != NULL &&
           strstr(pem, "-----END PRIVATE KEY-----") != NULL;
}

static int64_t now_ms(void)
{
#ifdef NETEASE_OFFICIAL_HOST_TEST
    return 1724918400000LL;
#elif defined(ESP_PLATFORM)
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000LL) + ((int64_t)tv.tv_usec / 1000LL);
#else
    return (int64_t)time(NULL) * 1000LL;
#endif
}

static void copy_limited(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void copy_json_text(cJSON *object, const char *key, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) return;
    dst[0] = '\0';
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        copy_limited(dst, dst_size, item->valuestring);
    } else if (cJSON_IsNumber(item)) {
        snprintf(dst, dst_size, "%.0f", item->valuedouble);
    }
}

static int json_int(cJSON *object, const char *key, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

static int64_t expiry_from_seconds(int seconds)
{
    if (seconds <= 0) return 0;
    return now_ms() + ((int64_t)seconds * 1000LL);
}

static esp_err_t append_raw(char *out, size_t out_size, size_t *len, const char *text)
{
    if (out == NULL || len == NULL || text == NULL) return ESP_ERR_INVALID_ARG;
    size_t text_len = strlen(text);
    if (*len + text_len >= out_size) {
        if (out_size > 0) out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out + *len, text, text_len);
    *len += text_len;
    out[*len] = '\0';
    return ESP_OK;
}

static esp_err_t append_encoded_param(char *query,
                                      size_t query_size,
                                      size_t *len,
                                      const char *key,
                                      const char *value,
                                      bool first)
{
    if (!first) {
        esp_err_t err = append_raw(query, query_size, len, "&");
        if (err != ESP_OK) return err;
    }
    esp_err_t err = append_raw(query, query_size, len, key);
    if (err != ESP_OK) return err;
    err = append_raw(query, query_size, len, "=");
    if (err != ESP_OK) return err;

    char encoded[NETEASE_OFFICIAL_ENCODED_VALUE_LEN] = {0};
    err = official_url_encode(value, encoded, sizeof(encoded));
    if (err != ESP_OK) return err;
    return append_raw(query, query_size, len, encoded);
}

static esp_err_t build_signed_query(netease_official_client_t *client,
                                    const char *biz_content,
                                    const char *access_token,
                                    char *query,
                                    size_t query_size)
{
    if (client == NULL || biz_content == NULL || query == NULL || query_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char timestamp[24] = {0};
    char device[NETEASE_OFFICIAL_DEVICE_LEN] = {0};
    char canonical[NETEASE_OFFICIAL_CANONICAL_LEN] = {0};
    char signature[NETEASE_OFFICIAL_SIGNATURE_LEN] = {0};
    snprintf(timestamp, sizeof(timestamp), "%" PRId64, now_ms());

    esp_err_t err = official_build_device_json(client->config.device_id,
                                               client->config.app_version,
                                               device,
                                               sizeof(device));
    if (err != ESP_OK) return err;

	    official_param_t params[] = {
	        {"accessToken", access_token == NULL ? "" : access_token},
	        {"appId", client->config.app_id},
	        {"appSecret", client->config.app_secret},
	        {"bizContent", biz_content},
	        {"device", device},
	        {"signType", NETEASE_OFFICIAL_SIGN_TYPE},
	        {"timestamp", timestamp},
	    };
    err = official_build_canonical(params, sizeof(params) / sizeof(params[0]), canonical, sizeof(canonical));
    if (err != ESP_OK) return err;

#ifdef NETEASE_OFFICIAL_HOST_TEST
    if (client->test_transport != NULL) {
        copy_limited(signature, sizeof(signature), "host-test-sign");
    } else
#endif
    {
        err = official_sign_rsa_sha256_base64(client->config.private_key_pem,
                                              canonical,
                                              signature,
                                              sizeof(signature));
        if (err != ESP_OK) return err;
    }

    size_t len = 0;
    query[0] = '\0';
    bool first = true;
    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        if (params[i].value == NULL || params[i].value[0] == '\0') continue;
        err = append_encoded_param(query, query_size, &len, params[i].key, params[i].value, first);
        if (err != ESP_OK) return err;
        first = false;
    }
    return append_encoded_param(query, query_size, &len, "sign", signature, first);
}

static esp_err_t call_api(netease_official_client_t *client,
                          const char *path,
                          const char *biz_content,
                          const char *access_token,
                          official_api_result_t *result)
{
    if (client == NULL || path == NULL || biz_content == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *query = calloc(1, NETEASE_OFFICIAL_QUERY_LEN);
    char *response = calloc(1, NETEASE_OFFICIAL_RESPONSE_MAX_BYTES + 1);
    if (query == NULL || response == NULL) {
        free(query);
        free(response);
        return ESP_ERR_NO_MEM;
    }

    int http_status = 0;
    esp_err_t err = build_signed_query(client, biz_content, access_token, query, NETEASE_OFFICIAL_QUERY_LEN);
    if (err != ESP_OK) goto cleanup;

#ifdef NETEASE_OFFICIAL_HOST_TEST
    if (client->test_transport != NULL) {
        err = client->test_transport(client,
                                     path,
                                     HTTP_METHOD_POST,
                                     query,
                                     response,
                                     NETEASE_OFFICIAL_RESPONSE_MAX_BYTES + 1,
                                     &http_status);
    } else
#endif
    {
        err = official_http_request(client,
                                    path,
                                    HTTP_METHOD_POST,
                                    query,
                                    response,
                                    NETEASE_OFFICIAL_RESPONSE_MAX_BYTES + 1,
                                    &http_status);
    }
    if (err != ESP_OK) goto cleanup;
    err = official_parse_envelope(response, http_status, result);

cleanup:
    free(query);
    free(response);
    return err;
}

static esp_err_t fill_token_set(cJSON *object, netease_token_set_t *out, const char *expiry_key)
{
    if (object == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    copy_json_text(object, "accessToken", out->access_token, sizeof(out->access_token));
    copy_json_text(object, "refreshToken", out->refresh_token, sizeof(out->refresh_token));
    int expire_seconds = json_int(object, expiry_key, 0);
    if (expire_seconds <= 0 && strcmp(expiry_key, "expiresTime") != 0) {
        expire_seconds = json_int(object, "expiresTime", 0);
    }
    if (expire_seconds <= 0 && strcmp(expiry_key, "expireTime") != 0) {
        expire_seconds = json_int(object, "expireTime", 0);
    }
    out->access_expires_at_ms = expiry_from_seconds(expire_seconds);
    out->refresh_expires_at_ms = expiry_from_seconds(NETEASE_OFFICIAL_REFRESH_SECONDS);
    if (out->access_token[0] == '\0' || out->refresh_token[0] == '\0') {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t parse_qr_token(cJSON *data, netease_official_qr_status_t *out)
{
    cJSON *access = cJSON_GetObjectItemCaseSensitive(data, "accessToken");
    if (!cJSON_IsObject(access)) return ESP_ERR_INVALID_RESPONSE;
    return fill_token_set(access, &out->tokens, "expireTime");
}

static bool playlist_exists(const netease_official_playlist_list_t *out, const char *id)
{
    for (size_t i = 0; i < out->count; i++) {
        if (strcmp(out->items[i].id, id) == 0) return true;
    }
    return false;
}

static esp_err_t append_playlists_from_data(cJSON *data, netease_official_playlist_list_t *out)
{
    cJSON *records = cJSON_GetObjectItemCaseSensitive(data, "records");
    if (!cJSON_IsArray(records)) return ESP_ERR_INVALID_RESPONSE;

    cJSON *record = NULL;
    cJSON_ArrayForEach(record, records) {
        if (!cJSON_IsObject(record)) continue;
        char id[NETEASE_OFFICIAL_ID_LEN] = {0};
        copy_json_text(record, "id", id, sizeof(id));
        if (id[0] == '\0' || playlist_exists(out, id)) continue;
        if (out->count >= NETEASE_OFFICIAL_MAX_PLAYLISTS) {
            out->has_more = true;
            break;
        }
        netease_official_playlist_t *item = &out->items[out->count++];
        copy_limited(item->id, sizeof(item->id), id);
        copy_json_text(record, "name", item->name, sizeof(item->name));
        copy_json_text(record, "creatorNickName", item->owner_name, sizeof(item->owner_name));
        item->track_count = json_int(record, "trackCount", 0);
    }
    int record_count = json_int(data, "recordCount", 0);
    if (record_count > 0 && record_count > (int)cJSON_GetArraySize(records)) {
        out->has_more = true;
    }
    return ESP_OK;
}

static void copy_first_artist(cJSON *track, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) return;
    out[0] = '\0';
    cJSON *artists = cJSON_GetObjectItemCaseSensitive(track, "artists");
    if (!cJSON_IsArray(artists)) return;
    cJSON *first = cJSON_GetArrayItem(artists, 0);
    if (!cJSON_IsObject(first)) return;
    copy_json_text(first, "name", out, out_size);
}

esp_err_t netease_official_init(netease_official_client_t *client,
                                const netease_official_config_t *config)
{
    if (client == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (is_empty_or_placeholder(config->app_id) ||
        is_empty_or_placeholder(config->app_secret) ||
        is_empty_or_placeholder(config->private_key_pem) ||
        is_empty_or_placeholder(config->device_id) ||
        is_empty_or_placeholder(config->app_version) ||
        !has_private_key_markers(config->private_key_pem)) {
        memset(client, 0, sizeof(*client));
        return ESP_ERR_INVALID_STATE;
    }

    memset(client, 0, sizeof(*client));
    client->config = *config;
    if (client->config.timeout_ms <= 0) {
        client->config.timeout_ms = NETEASE_OFFICIAL_DEFAULT_TIMEOUT_MS;
    }
    return ESP_OK;
}

#ifdef NETEASE_OFFICIAL_HOST_TEST
void netease_official_set_test_transport(netease_official_client_t *client,
                                         netease_official_transport_fn transport)
{
    if (client == NULL) return;
    client->test_transport = transport;
}
#endif

esp_err_t netease_official_anonymous_login(netease_official_client_t *client,
                                           char *token,
                                           size_t token_size)
{
    if (client == NULL || token == NULL || token_size == 0) return ESP_ERR_INVALID_ARG;
    char biz[NETEASE_OFFICIAL_BIZ_LEN] = {0};
    snprintf(biz, sizeof(biz), "{\"clientId\":\"%s\"}", client->config.app_id);

    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_ANONYMOUS_LOGIN, biz, NULL, &result);
    if (err == ESP_OK) {
        copy_json_text(result.data, "accessToken", token, token_size);
        if (token[0] == '\0') err = ESP_ERR_INVALID_RESPONSE;
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_qr_start(netease_official_client_t *client,
                                    const char *anonymous_token,
                                    netease_official_qr_t *out)
{
    if (client == NULL || is_empty_or_placeholder(anonymous_token) || out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    const char *biz = "{\"type\":2,\"expiredKey\":\"300\"}";
    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_QR_START, biz, anonymous_token, &result);
    if (err == ESP_OK) {
        copy_json_text(result.data, "qrCodeUrl", out->qr_code_url, sizeof(out->qr_code_url));
        copy_json_text(result.data, "uniKey", out->unikey, sizeof(out->unikey));
        out->expires_in_seconds = 300;
        if (out->qr_code_url[0] == '\0' || out->unikey[0] == '\0') err = ESP_ERR_INVALID_RESPONSE;
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_qr_poll(netease_official_client_t *client,
                                   const char *anonymous_token,
                                   const char *unikey,
                                   netease_official_qr_status_t *out)
{
    if (client == NULL || is_empty_or_placeholder(anonymous_token) || is_empty_or_placeholder(unikey) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    char biz[NETEASE_OFFICIAL_BIZ_LEN] = {0};
    snprintf(biz, sizeof(biz), "{\"key\":\"%s\",\"clientId\":\"%s\"}", unikey, client->config.app_id);

    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_QR_POLL, biz, anonymous_token, &result);
    if (err == ESP_OK) {
        int status = json_int(result.data, "status", NETEASE_OFFICIAL_QR_FAILED);
        out->state = (netease_official_qr_state_t)status;
        copy_json_text(result.data, "msg", out->message, sizeof(out->message));
        if (out->state == NETEASE_OFFICIAL_QR_CONFIRMED) {
            err = parse_qr_token(result.data, out);
        }
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_refresh(netease_official_client_t *client,
                                   const char *refresh_token,
                                   netease_token_set_t *out)
{
    if (client == NULL || is_empty_or_placeholder(refresh_token) || out == NULL) return ESP_ERR_INVALID_ARG;
    char biz[NETEASE_OFFICIAL_BIZ_LEN] = {0};
    snprintf(biz,
             sizeof(biz),
             "{\"clientId\":\"%s\",\"clientSecret\":\"%s\",\"refreshToken\":\"%s\"}",
             client->config.app_id,
             client->config.app_secret,
             refresh_token);

    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_TOKEN_REFRESH, biz, NULL, &result);
    if (err == ESP_OK) {
        err = fill_token_set(result.data, out, "expiresTime");
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_get_playlists(netease_official_client_t *client,
                                         const char *access_token,
                                         netease_official_playlist_list_t *out)
{
    if (client == NULL || is_empty_or_placeholder(access_token) || out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    const char *created_biz = "{\"limit\":20,\"offset\":0}";
    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_PLAYLIST_CREATED, created_biz, access_token, &result);
    if (err == ESP_OK) {
        err = append_playlists_from_data(result.data, out);
    }
    official_api_result_clear(&result);
    if (err != ESP_OK) return err;

    const char *subed_biz = "{\"limit\":20,\"offset\":0,\"originalCoverFlag\":false}";
    err = call_api(client, NETEASE_OFFICIAL_PATH_PLAYLIST_SUBSCRIBED, subed_biz, access_token, &result);
    if (err == ESP_OK) {
        err = append_playlists_from_data(result.data, out);
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_get_tracks(netease_official_client_t *client,
                                      const char *access_token,
                                      const char *playlist_id,
                                      uint32_t limit,
                                      uint32_t offset,
                                      netease_official_track_list_t *out)
{
    if (client == NULL || is_empty_or_placeholder(access_token) || is_empty_or_placeholder(playlist_id) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (limit == 0 || limit > NETEASE_OFFICIAL_MAX_TRACKS) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    copy_limited(out->playlist_id, sizeof(out->playlist_id), playlist_id);

    char biz[NETEASE_OFFICIAL_BIZ_LEN] = {0};
    snprintf(biz,
             sizeof(biz),
             "{\"playlistId\":\"%s\",\"limit\":%" PRIu32 ",\"offset\":%" PRIu32 ",\"qualityFlag\":true}",
             playlist_id,
             limit,
             offset);

    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_PLAYLIST_TRACKS, biz, access_token, &result);
    if (err == ESP_OK) {
        cJSON *tracks = cJSON_GetObjectItemCaseSensitive(result.data, "tracks");
        if (!cJSON_IsArray(tracks)) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            cJSON *track = NULL;
            cJSON_ArrayForEach(track, tracks) {
                if (!cJSON_IsObject(track)) continue;
                if (out->count >= NETEASE_OFFICIAL_MAX_TRACKS) {
                    out->has_more = true;
                    break;
                }
                netease_official_track_t *item = &out->items[out->count++];
                copy_json_text(track, "id", item->id, sizeof(item->id));
                copy_json_text(track, "name", item->name, sizeof(item->name));
                copy_first_artist(track, item->artist, sizeof(item->artist));
                item->duration_ms = json_int(track, "duration", 0);
            }
            int track_count = json_int(result.data, "trackCount", 0);
            uint32_t next = offset + limit;
            if (track_count > 0 && next < (uint32_t)track_count) {
                out->has_more = true;
                out->next_offset = next;
            }
        }
    }
    official_api_result_clear(&result);
    return err;
}

esp_err_t netease_official_get_play_url(netease_official_client_t *client,
                                        const char *access_token,
                                        const char *song_id,
                                        int bitrate,
                                        netease_official_play_url_t *out)
{
    if (client == NULL || is_empty_or_placeholder(access_token) || is_empty_or_placeholder(song_id) || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (bitrate <= 0) bitrate = 128000;
    memset(out, 0, sizeof(*out));
    copy_limited(out->song_id, sizeof(out->song_id), song_id);

    char biz[NETEASE_OFFICIAL_BIZ_LEN] = {0};
    snprintf(biz, sizeof(biz), "{\"songId\":\"%s\",\"bitrate\":%d}", song_id, bitrate);

    official_api_result_t result = {0};
    esp_err_t err = call_api(client, NETEASE_OFFICIAL_PATH_PLAY_URL, biz, access_token, &result);
    if (err == ESP_OK) {
        copy_json_text(result.data, "url", out->url, sizeof(out->url));
        out->expires_in_seconds = NETEASE_OFFICIAL_PLAY_URL_SECONDS;
        int br = json_int(result.data, "br", 0);
        if (br > 0) {
            copy_limited(out->format, sizeof(out->format), "mp3");
        }
        if (out->url[0] == '\0') {
            err = ESP_ERR_NOT_FOUND;
        }
    }
    official_api_result_clear(&result);
    return err;
}
