#include "netease_direct_client.h"

#include "cJSON.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/bignum.h"
#include "mbedtls/md5.h"
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char preset_key[] = "0CoJUm6Qyw8W8jud";
static const char eapi_key[] = "e82ckenh8dichen8";
static const unsigned char iv[16] = {
    '0','1','0','2','0','3','0','4','0','5','0','6','0','7','0','8'
};
static const unsigned char rsa_modulus[128] = {
    0xe0,0xb5,0x09,0xf6,0x25,0x9d,0xf8,0x64,0x2d,0xbc,0x35,0x66,0x29,0x01,0x47,
    0x7d,0xf2,0x26,0x77,0xec,0x15,0x2b,0x5f,0xf6,0x8a,0xce,0x61,0x5b,0xb7,0xb7,
    0x25,0x15,0x2b,0x3a,0xb1,0x7a,0x87,0x6a,0xea,0x8a,0x5a,0xa7,0x6d,0x2e,0x41,
    0x76,0x29,0xec,0x4e,0xe3,0x41,0xf5,0x61,0x35,0xfc,0xcf,0x69,0x52,0x80,0x10,
    0x4e,0x03,0x12,0xec,0xbd,0xa9,0x25,0x57,0xc9,0x38,0x70,0x11,0x4a,0xf6,0xc9,
    0xd0,0x5c,0x4f,0x7f,0x0c,0x36,0x85,0xb7,0xa4,0x6b,0xee,0x25,0x59,0x32,0x57,
    0x5c,0xce,0x10,0xb4,0x24,0xd8,0x13,0xcf,0xe4,0x87,0x5d,0x3e,0x82,0x04,0x7b,
    0x97,0xdd,0xef,0x52,0x74,0x1d,0x54,0x6b,0x8e,0x28,0x9d,0xc6,0x93,0x5b,0x3e,
    0xce,0x04,0x62,0xdb,0x0a,0x22,0xb8,0xe7
};

static esp_err_t copy_json_string(cJSON *object, const char *key, char *out, size_t out_size, bool required)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return required ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
    }
    /* Preserve complete UTF-8 characters when fitting display labels. */
    size_t n = strlen(item->valuestring);
    if (n >= out_size) {
        if (strcmp(key, "name") != 0 && strcmp(key, "nickname") != 0 && strcmp(key, "message") != 0) return ESP_ERR_INVALID_SIZE;
        n = out_size - 1;
        while (n && ((unsigned char)item->valuestring[n] & 0xc0) == 0x80) --n;
    }
    memcpy(out, item->valuestring, n); out[n] = 0;
    return ESP_OK;
}

static int64_t json_i64(cJSON *item)
{
    if (cJSON_IsNumber(item) && item->valuedouble >= 0 && item->valuedouble <= 9007199254740991.0 &&
        item->valuedouble == (double)(int64_t)item->valuedouble) return (int64_t)item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '-') {
        if (!isdigit((unsigned char)item->valuestring[0])) return 0;
        char *end = NULL; errno = 0;
        long long value = strtoll(item->valuestring, &end, 10);
        if (errno == 0 && end != item->valuestring && *end == '\0' && value > 0) return (int64_t)value;
    }
    return 0;
}

static cJSON *parse_root(const char *json)
{
    if (json == NULL) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &end, 1);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code) || (json_i64(code) != 200 && (json_i64(code) < 800 || json_i64(code) > 803))) { cJSON_Delete(root); return NULL; }
    return root;
}

esp_err_t netease_direct_parse_qr_key(const char *json, netease_direct_qr_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    esp_err_t err = copy_json_string(root, "unikey", out->key, sizeof(out->key), true);
    if (err == ESP_OK && out->key[0] != '\0' &&
        strspn(out->key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(out->key)) {
        int n = snprintf(out->payload, sizeof(out->payload), "https://music.163.com/login?codekey=%s", out->key);
        if (n < 0 || (size_t)n >= sizeof(out->payload)) err = ESP_ERR_INVALID_SIZE;
        out->expires_in_seconds = 180;
    } else if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
    cJSON_Delete(root);
    return err;
}

esp_err_t netease_direct_parse_qr_status(const char *json, netease_direct_qr_status_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    int code = (int)json_i64(cJSON_GetObjectItem(root, "code"));
    out->state = (code == 801 || code == 802 || code == 803 || code == 800)
        ? (netease_direct_qr_state_t)code : NETEASE_DIRECT_QR_FAILED;
    (void)copy_json_string(root, "message", out->message, sizeof(out->message), false);
    cJSON *profile = cJSON_GetObjectItem(root, "profile");
    if (cJSON_IsObject(profile)) (void)copy_json_string(profile, "nickname", out->nickname, sizeof(out->nickname), false);
    cJSON_Delete(root);
    return out->state != NETEASE_DIRECT_QR_FAILED ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t netease_direct_parse_qr_user_id(const char *json, int64_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    *out = 0;
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *profile = cJSON_GetObjectItem(root, "profile");
    *out = cJSON_IsObject(profile) ? json_i64(cJSON_GetObjectItem(profile, "userId")) : 0;
    cJSON_Delete(root);
    return *out > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t netease_direct_parse_playlists(const char *json, netease_direct_playlist_list_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *items = cJSON_GetObjectItem(root, "playlist");
    if (!cJSON_IsArray(items)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (out->count >= NETEASE_DIRECT_MAX_PLAYLISTS) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_SIZE; }
        if (!cJSON_IsObject(item)) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        netease_direct_playlist_t *playlist = &out->items[out->count];
        playlist->id = json_i64(cJSON_GetObjectItem(item, "id"));
        cJSON *count = cJSON_GetObjectItem(item, "trackCount");
        int64_t count_value = json_i64(count);
        if (!cJSON_IsNumber(count) || count->valuedouble != (double)count_value || count_value > INT_MAX) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        playlist->track_count = (int)count_value;
        if (playlist->id <= 0 || playlist->track_count < 0 || copy_json_string(item, "name", playlist->name, sizeof(playlist->name), true) != ESP_OK) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        out->count++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t netease_direct_parse_tracks(const char *json, int64_t playlist_id, netease_direct_track_list_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->playlist_id = playlist_id;
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *songs = cJSON_GetObjectItem(root, "songs");
    if (!cJSON_IsArray(songs)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, songs) {
        if (out->count >= NETEASE_DIRECT_MAX_TRACKS) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_SIZE; }
        if (!cJSON_IsObject(item)) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        netease_direct_track_t *track = &out->items[out->count];
        track->id = json_i64(cJSON_GetObjectItem(item, "id"));
        cJSON *duration = cJSON_GetObjectItem(item, "dt");
        int64_t duration_value = json_i64(duration);
        if (!cJSON_IsNumber(duration) || duration->valuedouble != (double)duration_value || duration_value > INT_MAX) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        track->duration_ms = (int)duration_value;
        cJSON *artists = cJSON_GetObjectItem(item, "ar");
        cJSON *artist = cJSON_IsArray(artists) ? cJSON_GetArrayItem(artists, 0) : NULL;
        if (track->id <= 0 || track->duration_ms < 0 || copy_json_string(item, "name", track->name, sizeof(track->name), true) != ESP_OK) { cJSON_Delete(root); memset(out, 0, sizeof(*out)); return ESP_ERR_INVALID_RESPONSE; }
        if (cJSON_IsObject(artist)) (void)copy_json_string(artist, "name", track->artist, sizeof(track->artist), false);
        out->count++;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t netease_direct_parse_track_url(const char *json, netease_direct_track_url_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_root(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *item = cJSON_IsArray(data) ? cJSON_GetArrayItem(data, 0) : NULL;
    if (!cJSON_IsObject(item)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    out->track_id = json_i64(cJSON_GetObjectItem(item, "id"));
    int64_t expires = json_i64(cJSON_GetObjectItem(item, "expi"));
    out->expires_in_seconds = expires <= INT_MAX ? (int)expires : 0;
    cJSON *resource=cJSON_GetObjectItem(item,"url");
    cJSON *resource_code=cJSON_GetObjectItem(item,"code");
    cJSON *resource_trial=cJSON_GetObjectItem(item,"freeTrialInfo");
    if(out->track_id>0 &&
       (cJSON_IsNull(resource) || (cJSON_IsString(resource) && resource->valuestring && !resource->valuestring[0]) ||
        (cJSON_IsNumber(resource_code) && (resource_code->valueint==404 || resource_code->valueint==403)) ||
        (resource_trial && !cJSON_IsNull(resource_trial)))) {
        cJSON_Delete(root);return NETEASE_DIRECT_ERR_UNAVAILABLE;
    }
    esp_err_t err = copy_json_string(item, "url", out->url, sizeof(out->url), true);
    if (err == ESP_OK) {
        cJSON *code = cJSON_GetObjectItem(item, "code");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        cJSON *trial = cJSON_GetObjectItem(item, "freeTrialInfo");
        if (out->track_id <= 0 || out->expires_in_seconds <= 0 ||
            (trial != NULL && !cJSON_IsNull(trial)) ||
            (code != NULL && json_i64(code) != 200) ||
            (strncmp(out->url, "https://", 8) != 0 && strncmp(out->url, "http://", 7) != 0)) err = ESP_ERR_INVALID_RESPONSE;
        const char *host = strstr(out->url, "://");
        if (host) {
            host += 3;
            size_t len = strcspn(host, "/?#");
            if (len == 0) err = ESP_ERR_INVALID_RESPONSE;
            for (size_t i = 0; i < len; ++i) if (!isalnum((unsigned char)host[i]) && host[i] != '.' && host[i] != '-') err = ESP_ERR_INVALID_RESPONSE;
        }
        if (err == ESP_OK && cJSON_IsString(type) && type->valuestring != NULL) {
            if (strlen(type->valuestring) >= sizeof(out->format)) err = ESP_ERR_INVALID_SIZE;
            else strcpy(out->format, type->valuestring);
        } else if (err == ESP_OK) {
            size_t path_len = strcspn(out->url, "?#");
            size_t path_start = 0;
            for (size_t i = 0; i < path_len; ++i) if (out->url[i] == '/') path_start = i + 1;
            size_t dot = path_len;
            for (size_t i = path_start; i < path_len; ++i) if (out->url[i] == '.') dot = i;
            if (dot == path_len || dot + 1 == path_len || path_len - dot - 1 >= sizeof(out->format)) err = ESP_ERR_INVALID_RESPONSE;
            else { memcpy(out->format, out->url + dot + 1, path_len - dot - 1); out->format[path_len - dot - 1] = 0; }
        }
        for (size_t i = 0; out->format[i]; ++i) out->format[i] = (char)tolower((unsigned char)out->format[i]);
        if (strcmp(out->format, "mp3") != 0) err = ESP_ERR_INVALID_RESPONSE;
        for (const unsigned char *p = (const unsigned char *)out->url; *p; ++p) if (*p <= 0x20 || *p == 0x7f) err = ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(root);
    if (err != ESP_OK) memset(out, 0, sizeof(*out));
    return err;
}

static esp_err_t aes_cbc_base64(const unsigned char *input, size_t input_len,
                                const unsigned char *key, char *out, size_t out_size)
{
    if (input == NULL || key == NULL || out == NULL || input_len > SIZE_MAX - 16) return ESP_ERR_INVALID_ARG;
    size_t padded = input_len + 16 - (input_len % 16);
    if (padded > (SIZE_MAX / 4) * 3 || out_size < ((padded + 2) / 3) * 4 + 1) return ESP_ERR_INVALID_SIZE;
    unsigned char *plain = malloc(padded);
    unsigned char *cipher = malloc(padded);
    if (plain == NULL || cipher == NULL) { free(plain); free(cipher); return ESP_ERR_NO_MEM; }
    memcpy(plain, input, input_len);
    memset(plain + input_len, (int)(padded - input_len), padded - input_len);
    mbedtls_aes_context aes; mbedtls_aes_init(&aes);
    unsigned char local_iv[16]; memcpy(local_iv, iv, sizeof(local_iv));
    int rc = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (rc == 0) rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded, local_iv, plain, cipher);
    size_t encoded = 0;
    if (rc == 0) rc = mbedtls_base64_encode((unsigned char *)out, out_size - 1, &encoded, cipher, padded);
    if (rc == 0) out[encoded] = '\0';
    mbedtls_aes_free(&aes); free(plain); free(cipher);
    return rc == 0 && encoded < out_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t url_encode(const char *input, char *out, size_t out_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        bool safe = isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = safe ? 1 : 3;
        if (used + need + 1 > out_size) return ESP_ERR_INVALID_SIZE;
        if (safe) out[used++] = (char)*p;
        else { out[used++] = '%'; out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 0xf]; }
    }
    out[used] = '\0';
    return ESP_OK;
}

static esp_err_t rsa_raw_hex(const unsigned char secret[16], char out[257])
{
    unsigned char block[128] = {0};
    for (int i = 0; i < 16; ++i) block[112 + i] = secret[15 - i];
    mbedtls_mpi n, e, m, c; mbedtls_mpi_init(&n); mbedtls_mpi_init(&e); mbedtls_mpi_init(&m); mbedtls_mpi_init(&c);
    int rc = mbedtls_mpi_read_binary(&n, rsa_modulus, sizeof(rsa_modulus));
    if (rc == 0) rc = mbedtls_mpi_lset(&e, 65537);
    if (rc == 0) rc = mbedtls_mpi_read_binary(&m, block, sizeof(block));
    if (rc == 0) rc = mbedtls_mpi_exp_mod(&c, &m, &e, &n, NULL);
    unsigned char result[128];
    if (rc == 0) rc = mbedtls_mpi_write_binary(&c, result, sizeof(result));
    mbedtls_mpi_free(&n); mbedtls_mpi_free(&e); mbedtls_mpi_free(&m); mbedtls_mpi_free(&c);
    if (rc != 0) return ESP_FAIL;
    for (size_t i = 0; i < sizeof(result); ++i) snprintf(out + i * 2, 3, "%02x", result[i]);
    return ESP_OK;
}

esp_err_t netease_direct_build_weapi_body_with_key(const char *json, const unsigned char secret[16], char *out, size_t out_size)
{
    if (json == NULL || secret == NULL || out == NULL || out_size < 64) return ESP_ERR_INVALID_ARG;
    size_t json_len = strlen(json);
    if (json_len > 1200) return ESP_ERR_INVALID_SIZE;
    size_t first_cap = ((json_len + 16) / 16) * 16 * 4 / 3 + 8;
    size_t params_cap = ((first_cap + 16) / 16) * 16 * 4 / 3 + 8;
    char *first = malloc(first_cap), *params = malloc(params_cap);
    char enc_key[257] = {0};
    if (first == NULL || params == NULL) { free(first); free(params); return ESP_ERR_NO_MEM; }
    esp_err_t err = aes_cbc_base64((const unsigned char *)json, json_len, (const unsigned char *)preset_key, first, first_cap);
    if (err == ESP_OK) err = aes_cbc_base64((const unsigned char *)first, strlen(first), secret, params, params_cap);
    /* The first AES layer is no longer needed during RSA or URL encoding. */
    free(first);
    if (err == ESP_OK) err = rsa_raw_hex(secret, enc_key);
    if (err == ESP_OK) {
        memcpy(out, "params=", 7);
        err = url_encode(params, out + 7, out_size - 7);
    }
    if (err == ESP_OK) {
        size_t used = strlen(out);
        int n = snprintf(out + used, out_size - used, "&encSecKey=%s", enc_key);
        if (n < 0 || (size_t)n >= out_size - used) err = ESP_ERR_INVALID_SIZE;
    }
    free(params);
    return err;
}

esp_err_t netease_direct_build_weapi_body(const char *json, char *out, size_t out_size)
{
    unsigned char secret[16];
#ifdef ESP_PLATFORM
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < sizeof(secret); ++i) secret[i] = (unsigned char)alphabet[esp_random() % (sizeof(alphabet) - 1)];
#else
    static const unsigned char test_secret[] = "0123456789abcdef";
    memcpy(secret, test_secret, sizeof(secret));
#endif
    return netease_direct_build_weapi_body_with_key(json, secret, out, out_size);
}

esp_err_t netease_direct_build_eapi_body(const char *path, const char *json, char *out, size_t out_size)
{
    if (path == NULL || json == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    size_t path_len = strlen(path), json_len = strlen(json);
    /* EAPI embeds MUSIC_U; the login token alone can exceed 1200 bytes. */
    if (path_len > 256 || json_len > NETEASE_DIRECT_COOKIE_MAX_BYTES + 1024) return ESP_ERR_INVALID_SIZE;
    size_t message_len = 6 + path_len + 3 + json_len + 13;
    char *message = malloc(message_len + 1);
    if (message == NULL) return ESP_ERR_NO_MEM;
    int written = snprintf(message, message_len + 1, "nobody%suse%smd5forencrypt", path, json);
    if (written < 0 || (size_t)written != message_len) { free(message); return ESP_ERR_INVALID_SIZE; }
    char digest[33];
    unsigned char md[16];
    mbedtls_md5_context md5; mbedtls_md5_init(&md5); int mdrc = mbedtls_md5_starts(&md5); if (mdrc == 0) mdrc = mbedtls_md5_update(&md5, (const unsigned char *)message, message_len); if (mdrc == 0) mdrc = mbedtls_md5_finish(&md5, md); mbedtls_md5_free(&md5); free(message); if (mdrc != 0) return ESP_FAIL;
    for (size_t i = 0; i < sizeof(md); ++i) snprintf(digest + i * 2, 3, "%02x", md[i]);
    size_t plain_len = path_len + 13 + json_len + 13 + 32;
    if (plain_len > 16384) return ESP_ERR_INVALID_SIZE;
    char *plain = malloc(plain_len + 16);
    if (plain == NULL) return ESP_ERR_NO_MEM;
    int length = snprintf(plain, plain_len + 1, "%s-36cd479b6b5-%s-36cd479b6b5-%s", path, json, digest);
    if (length < 0 || (size_t)length != plain_len) { free(plain); return ESP_ERR_INVALID_SIZE; }
    size_t padded = (size_t)length + 16 - ((size_t)length % 16);
    if (7 + padded * 2 + 1 > out_size) { free(plain); return ESP_ERR_INVALID_SIZE; }
    memset(plain + length, (int)(padded - (size_t)length), padded - (size_t)length);
    unsigned char *cipher = malloc(padded);
    if (cipher == NULL) { free(plain); return ESP_ERR_NO_MEM; }
    mbedtls_aes_context aes; mbedtls_aes_init(&aes); int rc = mbedtls_aes_setkey_enc(&aes, (const unsigned char *)eapi_key, 128);
    for (size_t i = 0; rc == 0 && i < padded; i += 16) rc = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char *)plain + i, cipher + i);
    mbedtls_aes_free(&aes);
    if (rc == 0) { memcpy(out, "params=", 7); for (size_t i = 0; i < padded; ++i) snprintf(out + 7 + i * 2, 3, "%02X", cipher[i]); }
    free(cipher); free(plain);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t netease_direct_cookie_copy(const char *cookie, char *out, size_t out_size)
{
    if (cookie == NULL || out == NULL || out_size == 0) return ESP_ERR_INVALID_ARG;
    size_t length = strlen(cookie);
    if (length == 0 || length >= out_size || length > NETEASE_DIRECT_COOKIE_MAX_BYTES) { out[0] = 0; return ESP_ERR_INVALID_SIZE; }
    for (size_t i = 0; i < length; ++i) {
        if ((unsigned char)cookie[i] < 0x20 || (unsigned char)cookie[i] == 0x7f) { out[0] = 0; return ESP_ERR_INVALID_RESPONSE; }
    }
    memcpy(out, cookie, length + 1);
    return ESP_OK;
}

bool netease_direct_cookie_has_login(const char *cookie)
{
    if (cookie == NULL || strlen(cookie) > NETEASE_DIRECT_COOKIE_MAX_BYTES) return false;
    for (const unsigned char *c = (const unsigned char *)cookie; *c; ++c) if (*c < 0x20 || *c == 0x7f) return false;
    const char *p = cookie;
    while (*p) {
        while (*p == ' ' || *p == ';') ++p;
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 8 && strncmp(p, "MUSIC_U=", 8) == 0) {
            for (size_t i = 8; i < len; ++i) if ((unsigned char)p[i] <= 0x20 || (unsigned char)p[i] == 0x7f) return false;
            return true;
        }
        p = end ? end + 1 : p + len;
    }
    return false;
}

esp_err_t netease_direct_cookie_merge(char *cookie, size_t capacity, const char *header)
{
    if (!cookie || !header || !capacity || strlen(cookie) >= capacity) return ESP_ERR_INVALID_ARG;
    size_t n = strcspn(header, ";");
    const char *eq = memchr(header, '=', n);
    if (!eq || eq == header || n > NETEASE_DIRECT_COOKIE_MAX_BYTES) return ESP_ERR_INVALID_RESPONSE;
    size_t key_len = (size_t)(eq - header);
    for (size_t i = 0; i < n; ++i) if ((unsigned char)header[i] <= 0x20 || (unsigned char)header[i] >= 0x7f) return ESP_ERR_INVALID_RESPONSE;
    char *next = calloc(1, capacity); if (!next) return ESP_ERR_NO_MEM;
    size_t used = 0; const char *p = cookie;
    while (*p) {
        while (*p == ';' || *p == ' ') ++p;
        size_t len = strcspn(p, ";");
        if (len && !(len > key_len && p[key_len] == '=' && memcmp(p, header, key_len) == 0)) {
            if (used + len + (used ? 2 : 0) >= capacity) { free(next); return ESP_ERR_INVALID_SIZE; }
            if (used) { memcpy(next + used, "; ", 2); used += 2; }
            memcpy(next + used, p, len); used += len;
        }
        p += len;
    }
    if (n > key_len + 1) {
        if (used + n + (used ? 2 : 0) >= capacity || used + n + (used ? 2 : 0) > NETEASE_DIRECT_COOKIE_MAX_BYTES) { free(next); return ESP_ERR_INVALID_SIZE; }
        if (used) { memcpy(next + used, "; ", 2); used += 2; }
        memcpy(next + used, header, n); used += n;
    }
    next[used] = 0; memcpy(cookie, next, used + 1); free(next); return ESP_OK;
}

char *netease_direct_session_json(const char *json, const char *cookie, bool eapi)
{
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    char csrf[256] = {0};
    char *music_u = calloc(1, NETEASE_DIRECT_COOKIE_MAX_BYTES + 1);
    if (!music_u) { cJSON_Delete(root); return NULL; }
    const char *p = cookie ? cookie : "";
    while (*p) {
        while (*p == ' ' || *p == ';') ++p;
        const char *end = strchr(p, ';'); if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        if (eq) {
            char *dest = NULL; size_t cap = 0;
            if (eq - p == 6 && strncmp(p, "__csrf", 6) == 0) { dest = csrf; cap = sizeof(csrf); }
            if (eq - p == 7 && strncmp(p, "MUSIC_U", 7) == 0) { dest = music_u; cap = NETEASE_DIRECT_COOKIE_MAX_BYTES + 1; }
            if (dest) { size_t n = (size_t)(end - eq - 1); if (n >= cap) { cJSON_Delete(root); free(music_u); return NULL; } memset(dest, 0, cap); memcpy(dest, eq + 1, n); }
        }
        p = end;
    }
    cJSON *target = root;
    if (eapi) {
        target = cJSON_GetObjectItemCaseSensitive(root, "header");
        if (!cJSON_IsObject(target)) { cJSON_Delete(root); free(music_u); return NULL; }
    }
    bool ok = cJSON_AddStringToObject(target, eapi ? "__csrf" : "csrf_token", csrf) != NULL;
    if (eapi && music_u[0]) ok = ok && cJSON_AddStringToObject(target, "MUSIC_U", music_u) != NULL;
    char *result = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root); memset(music_u, 0, NETEASE_DIRECT_COOKIE_MAX_BYTES + 1); free(music_u); return result;
}
