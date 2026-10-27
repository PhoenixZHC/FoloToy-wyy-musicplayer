#include "netease_direct_client.h"
#include "direct_stream.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define SESSION_NS "netease_session"
#define BODY_CAP 8192
#define RESPONSE_CAP 16384
#define HTTP_TX_BUFFER_SIZE 4096
static netease_direct_session_t session;
static int timeout_ms = 15000;
/* Single worker owns one same-origin WEAPI connection; never shared with EAPI. */
static esp_http_client_handle_t reusable_http;
static void release_http(void)
{
    if(reusable_http) { esp_http_client_cleanup(reusable_http);reusable_http=NULL; }
}
/* Single-worker bounded ID cache. Cleared whenever the account changes. */
#define PLAYLIST_ID_CACHE_CAP 512
static struct { int64_t playlist, user; uint32_t total, start, count; int64_t *ids; time_t loaded_at; } id_cache;
static void clear_id_cache(void) { free(id_cache.ids); memset(&id_cache,0,sizeof(id_cache)); }


/* Public request metadata follows the pinned api-enhanced EAPI transport. */
static char *eapi_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
    if (!header) header = cJSON_AddObjectToObject(root, "header");
    if (!cJSON_IsObject(header)) { cJSON_Delete(root); return NULL; }
    struct timeval now;
    unsigned char mac[6];
    if (gettimeofday(&now, NULL) != 0 || esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) { cJSON_Delete(root); return NULL; }
    char device[18], build[24], request_id[40];
    static unsigned sequence;
    snprintf(device, sizeof(device), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(build, sizeof(build), "%lld", (long long)now.tv_sec);
    snprintf(request_id, sizeof(request_id), "%lld_%04u", (long long)now.tv_sec * 1000 + now.tv_usec / 1000, sequence++ % 10000);
    const char *fields[][2] = {
        {"os", "pc"}, {"appver", "3.1.17.204416"},
        {"osver", "Microsoft-Windows-10-Professional-build-19045-64bit"},
        {"channel", "netease"}, {"deviceId", device}, {"versioncode", "140"},
        {"mobilename", ""}, {"buildver", build}, {"resolution", "1920x1080"}, {"requestId", request_id},
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (!cJSON_GetObjectItemCaseSensitive(header, fields[i][0]))
            ok = ok && cJSON_AddStringToObject(header, fields[i][0], fields[i][1]) != NULL;
    /* Preserve anonymous/device cookies returned by previous EAPI calls. */
    for (const char *p = session.cookie; *p;) {
        while (*p == ' ' || *p == ';') ++p;
        const char *end = strchr(p, ';'); if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        const char *name = eq && eq - p == 7 && strncmp(p, "MUSIC_A", 7) == 0 ? "MUSIC_A" :
                           eq && eq - p == 5 && strncmp(p, "NMTID", 5) == 0 ? "NMTID" : NULL;
        if (name) {
            size_t n = (size_t)(end - eq - 1);
            char *value = malloc(n + 1);
            if (!value) ok = false;
            else { memcpy(value, eq + 1, n); value[n] = 0; ok = ok && cJSON_AddStringToObject(header, name, value) != NULL; free(value); }
        }
        p = end;
    }
    if (!cJSON_GetObjectItemCaseSensitive(root, "e_r")) ok = ok && cJSON_AddBoolToObject(root, "e_r", false) != NULL;
    char *result = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root); return result;
}

static bool eapi_cookie(const char *json, char *out, size_t capacity)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
    size_t used = 0; bool ok = cJSON_IsObject(header);
    cJSON *item;
    cJSON_ArrayForEach(item, header) {
        if (!ok || !item->string || !cJSON_IsString(item)) { ok = false; break; }
        const char *parts[] = {item->string, item->valuestring};
        if (used) { if (used + 2 >= capacity) { ok = false; break; } out[used++] = ';'; out[used++] = ' '; }
        for (size_t part = 0; part < 2 && ok; ++part) {
            if (part) { if (used + 1 >= capacity) { ok = false; break; } out[used++] = '='; }
            for (const unsigned char *p = (const unsigned char *)parts[part]; *p; ++p) {
                bool safe = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-_.!~*'()", *p);
                size_t n = safe ? 1 : 3;
                if (used + n >= capacity) { ok = false; break; }
                if (safe) out[used++] = (char)*p;
                else { static const char hex[] = "0123456789ABCDEF"; out[used++] = '%'; out[used++] = hex[*p >> 4]; out[used++] = hex[*p & 15]; }
            }
        }
    }
    if (capacity) out[used] = 0;
    cJSON_Delete(root); return ok;
}

typedef struct {
    char *response;
    size_t used, capacity;
    direct_stream_t *stream;
    char *cookies;
    esp_err_t error;
} http_context_t;

static esp_err_t on_http(esp_http_client_event_t *event)
{
    http_context_t *c = event->user_data;
    if(!c)return ESP_OK;
    if (c->error != ESP_OK) return c->error;
    if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "Set-Cookie") == 0) {
        c->error = netease_direct_cookie_merge(c->cookies, sizeof(session.cookie), event->header_value);
        if (c->error != ESP_OK) ESP_LOGW("netease_direct", "Cookie merge failed: err=%d stored_bytes=%u incoming_bytes=%u", c->error, (unsigned)strlen(c->cookies), (unsigned)strlen(event->header_value));
    } else if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (c->stream) {
            c->error = direct_stream_feed(c->stream, event->data, (size_t)event->data_len);
            if (c->error != ESP_OK) ESP_LOGW("netease_direct", "Stream failed: err=%d bytes=%u depth=%u role=%u token_bytes=%u playlists=%u", c->error, (unsigned)c->stream->bytes, (unsigned)c->stream->depth, c->stream->depth ? c->stream->frames[c->stream->depth - 1].role : 0, (unsigned)c->stream->token_len, c->stream->playlists ? (unsigned)c->stream->playlists->count : 0);
        }
        else if ((size_t)event->data_len >= c->capacity - c->used) c->error = ESP_ERR_INVALID_SIZE;
        else { memcpy(c->response + c->used, event->data, (size_t)event->data_len); c->used += (size_t)event->data_len; c->response[c->used] = 0; }
    }
    return c->error;
}

/* Only fixed NetEase API origins are accepted; redirects never forward Cookie. */
static esp_err_t request(const char *path, const char *json, bool eapi, char **response, direct_stream_t *stream)
{
    struct timeval started;gettimeofday(&started,NULL);
    char url[192];
    const char *prefix = eapi ? "https://interfacepc.music.163.com/eapi" : "https://music.163.com";
    if (eapi && strncmp(path, "/api/", 5) != 0) return ESP_ERR_INVALID_ARG;
    int n = snprintf(url, sizeof(url), "%s%s", prefix, eapi ? path + 4 : path);
    if (n < 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    /* Four-song WEAPI requests do not need the larger EAPI cookie envelope. */
    size_t body_capacity=eapi?BODY_CAP:2048;
    char *body = malloc(body_capacity);
    char *cookies = malloc(sizeof(session.cookie));
    char *buffer = stream ? NULL : calloc(1, RESPONSE_CAP);
    if (!body || !cookies || (!stream && !buffer)) { free(body); free(cookies); free(buffer); return ESP_ERR_NO_MEM; }
    memcpy(cookies, session.cookie, sizeof(session.cookie));
    char *metadata = eapi ? eapi_json(json) : NULL;
    char *payload = (!eapi || metadata) ? netease_direct_session_json(eapi ? metadata : json, session.cookie, eapi) : NULL;
    free(metadata);
    char *header_cookie = eapi ? malloc(BODY_CAP) : NULL;
    if (eapi && payload) ESP_LOGI("netease_direct", "EAPI request: path=%s json_bytes=%u", path, (unsigned)strlen(payload));
    esp_err_t err = payload ? (eapi ? netease_direct_build_eapi_body(path, payload, body, BODY_CAP) : netease_direct_build_weapi_body(payload, body, body_capacity)) : ESP_ERR_NO_MEM;
    if (err == ESP_OK && eapi) err = !header_cookie ? ESP_ERR_NO_MEM : eapi_cookie(payload, header_cookie, BODY_CAP) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    free(payload);
    if(err!=ESP_OK)ESP_LOGW("netease_direct","Request preparation failed: path=%s err=%d",path,err);
    if(err==ESP_OK) {
        char *compact=realloc(body,strlen(body)+1);
        if(compact)body=compact;
    }
    http_context_t context = {.response = buffer, .capacity = RESPONSE_CAP, .stream = stream, .cookies = cookies};
    esp_http_client_config_t cfg = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = timeout_ms,
        /* Keep the largest allowed Cookie header and request headers in one TX buffer. */
        .buffer_size = 2048, .buffer_size_tx = HTTP_TX_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .event_handler = on_http, .user_data = &context,
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER && CONFIG_ESP_TLS_DYN_BUF_STRATEGY_SUPPORTED
        /* ESP-IDF reserves the full incoming record after authentication.
         * Allocate it before starting audio, not during a fragmented-heap read. */
        .tls_dyn_buf_strategy = eapi ? 0 : HTTP_TLS_DYN_BUF_RX_STATIC,
#endif
    };
    bool reused=!eapi && reusable_http!=NULL;
    if(eapi)release_http();
    esp_http_client_handle_t h = err == ESP_OK ? (!eapi && reusable_http?reusable_http:esp_http_client_init(&cfg)) : NULL;
    if (err == ESP_OK && !h) err = ESP_ERR_NO_MEM;
    if (h) {
        err=esp_http_client_set_user_data(h,&context);
        if(err==ESP_OK)err=esp_http_client_set_url(h,url);
        if (err == ESP_OK) err = esp_http_client_set_header(h, "Content-Type", "application/x-www-form-urlencoded");
        if (err == ESP_OK) err = esp_http_client_set_header(h, "Referer", "https://music.163.com/");
        if (err == ESP_OK) err = esp_http_client_set_header(h, "Origin", "https://music.163.com");
        if (err == ESP_OK) err = esp_http_client_set_header(h, "User-Agent", eapi ? "NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)" : "Mozilla/5.0");
        if (err == ESP_OK) err = esp_http_client_set_header(h, "Cookie", eapi ? header_cookie : session.cookie);
        if (err == ESP_OK) err = esp_http_client_set_post_field(h, body, (int)strlen(body));
        if (err == ESP_OK) err = esp_http_client_perform(h);
        /* A server may retire an idle keep-alive socket. Replay only read-only
         * WEAPI lookups, before any response data/cookie was accepted. Never
         * replay login, EAPI or a partially parsed body. */
        bool read_only = strcmp(path,"/weapi/nuser/account/get")==0 ||
            strcmp(path,"/weapi/user/playlist")==0 ||
            strcmp(path,"/weapi/v6/playlist/detail")==0 ||
            strcmp(path,"/weapi/v3/song/detail")==0 ||
            strcmp(path,"/weapi/song/lyric")==0;
        if(reused && read_only && (err==ESP_ERR_HTTP_FETCH_HEADER || err==ESP_ERR_HTTP_WRITE_DATA) && context.error==ESP_OK &&
           context.used==0 && (!stream || stream->bytes==0) && strcmp(cookies,session.cookie)==0) {
            ESP_LOGW("netease_direct","Idle API connection closed; rebuilding once: path=%s",path);
            release_http();h=esp_http_client_init(&cfg);
            err=h?ESP_OK:ESP_ERR_NO_MEM;
            const char *headers[][2]={{"Content-Type","application/x-www-form-urlencoded"},
                {"Referer","https://music.163.com/"},{"Origin","https://music.163.com"},
                {"User-Agent","Mozilla/5.0"},{"Cookie",session.cookie}};
            for(size_t i=0;err==ESP_OK && i<sizeof(headers)/sizeof(headers[0]);i++)
                err=esp_http_client_set_header(h,headers[i][0],headers[i][1]);
            if(err==ESP_OK)err=esp_http_client_set_post_field(h,body,(int)strlen(body));
            if(err==ESP_OK)err=esp_http_client_perform(h);
        }
        if (context.error != ESP_OK) err = context.error;
        int status = h?esp_http_client_get_status_code(h):0;
        if (err != ESP_OK || status != 200) ESP_LOGW("netease_direct", "Request failed: path=%s http=%d err=%d bytes=%u", path, status, err, (unsigned)context.used);
        if (err == ESP_OK && status == 401) err = NETEASE_DIRECT_ERR_AUTH;
        if (err == ESP_OK && status != 200) err = ESP_ERR_INVALID_RESPONSE;
        /* No request-local pointers may remain on a retained client. */
        if(h) { esp_http_client_set_user_data(h,NULL);esp_http_client_set_post_field(h,NULL,0); }
        if(!eapi && err==ESP_OK)reusable_http=h;
        else if(h) { if(h==reusable_http)reusable_http=NULL;esp_http_client_cleanup(h); }
    }
    if (err == ESP_OK && stream) err = direct_stream_finish(stream);
    if (err == ESP_OK && !stream) {
        cJSON *root = cJSON_ParseWithOpts(buffer, NULL, true);
        if (!cJSON_IsObject(root)) err = ESP_ERR_INVALID_RESPONSE;
        cJSON *code = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
        if (!cJSON_IsNumber(code) || code->valuedouble != 200) ESP_LOGI("netease_direct", "Response: path=%s code=%d object=%d bytes=%u login_cookie=%d", path, cJSON_IsNumber(code) ? code->valueint : -1, cJSON_IsObject(root), (unsigned)context.used, netease_direct_cookie_has_login(cookies));
        if (cJSON_IsNumber(code) && (code->valuedouble == 301 || code->valuedouble == 302 || code->valuedouble == 401)) err = NETEASE_DIRECT_ERR_AUTH;
        else if (cJSON_IsNumber(code) && code->valuedouble == 8821 && strncmp(path, "/api/login/qrcode/", 18) == 0) err = NETEASE_DIRECT_ERR_LOGIN_REJECTED;
        else if (!cJSON_IsNumber(code) || (code->valuedouble != 200 && code->valuedouble != 800 && code->valuedouble != 801 && code->valuedouble != 802 && code->valuedouble != 803)) err = ESP_ERR_INVALID_RESPONSE;
        cJSON_Delete(root);
    }
    if (err == ESP_OK) memcpy(session.cookie, cookies, sizeof(session.cookie));
    if(err!=ESP_OK)release_http();
    free(body); free(cookies); free(header_cookie);
    struct timeval finished;gettimeofday(&finished,NULL);
    int64_t elapsed=(int64_t)(finished.tv_sec-started.tv_sec)*1000+(finished.tv_usec-started.tv_usec)/1000;
    ESP_LOGI("netease_direct","API timing: path=%s elapsed_ms=%lld bytes=%u err=%d",path,(long long)elapsed,(unsigned)(stream?stream->bytes:context.used),err);
    if (response) *response = err == ESP_OK ? buffer : NULL;
    if (err != ESP_OK || !response) free(buffer);
    return err;
}

esp_err_t netease_direct_client_init(const netease_direct_client_config_t *config)
{
    if (!config || config->timeout_ms < 1000 || config->timeout_ms > 60000) return ESP_ERR_INVALID_ARG;
    release_http();timeout_ms = config->timeout_ms; return ESP_OK;
}

esp_err_t netease_direct_session_load(netease_direct_session_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out)); memset(&session, 0, sizeof(session));
    nvs_handle_t h; esp_err_t err = nvs_open(SESSION_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t n = sizeof(session); err = nvs_get_blob(h, "v1", &session, &n); nvs_close(h);
    if (err == ESP_OK && (n != sizeof(session) || !memchr(session.cookie, 0, sizeof(session.cookie)) ||
        !memchr(session.nickname, 0, sizeof(session.nickname)) || session.user_id <= 0 ||
        !netease_direct_cookie_has_login(session.cookie))) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) { memset(&session, 0, sizeof(session)); if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND; return err; }
    memcpy(out, &session, sizeof(*out)); return ESP_OK;
}

esp_err_t netease_direct_session_save(const char *cookie, int64_t user_id, const char *nickname)
{
    if (!cookie || user_id <= 0 || !nickname || strlen(nickname) >= sizeof(session.nickname) || !netease_direct_cookie_has_login(cookie)) return ESP_ERR_INVALID_ARG;
    netease_direct_session_t *next = calloc(1, sizeof(*next)); if (!next) return ESP_ERR_NO_MEM;
    esp_err_t err = netease_direct_cookie_copy(cookie, next->cookie, sizeof(next->cookie));
    next->user_id = user_id; strcpy(next->nickname, nickname);
    nvs_handle_t h;
    if (err == ESP_OK) {
        err = nvs_open(SESSION_NS, NVS_READWRITE, &h);
        if (err == ESP_OK) { err = nvs_set_blob(h, "v1", next, sizeof(*next)); if (err == ESP_OK) err = nvs_commit(h); nvs_close(h); }
    }
    if (err == ESP_OK) { if(session.user_id!=next->user_id)clear_id_cache();memcpy(&session, next, sizeof(session)); }
    memset(next, 0, sizeof(*next)); free(next); return err;
}

esp_err_t netease_direct_session_clear(void)
{
    release_http();
    nvs_handle_t h; esp_err_t err = nvs_open(SESSION_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, "v1"); if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) { clear_id_cache();memset(&session, 0, sizeof(session)); }
    return err;
}

esp_err_t netease_direct_session_verify(void)
{
    if (!netease_direct_cookie_has_login(session.cookie)) return NETEASE_DIRECT_ERR_AUTH;
    char *response = NULL; esp_err_t err = request("/weapi/nuser/account/get", "{}", false, &response, NULL);
    int64_t uid = 0; char nickname[NETEASE_DIRECT_NAME_MAX] = {0};
    if (err == ESP_OK) {
        err = netease_direct_parse_qr_user_id(response, &uid);
        cJSON *root = cJSON_Parse(response); cJSON *profile = cJSON_IsObject(root) ? cJSON_GetObjectItem(root, "profile") : NULL;
        cJSON *name = cJSON_IsObject(profile) ? cJSON_GetObjectItem(profile, "nickname") : NULL;
        if (err != ESP_OK) ESP_LOGW("netease_direct", "Account schema: profile_type=%d account_type=%d user_id_type=%d", profile ? profile->type : -1, cJSON_GetObjectItem(root, "account") ? cJSON_GetObjectItem(root, "account")->type : -1, cJSON_IsObject(profile) && cJSON_GetObjectItem(profile, "userId") ? cJSON_GetObjectItem(profile, "userId")->type : -1);
        if (cJSON_IsString(name)) { size_t n = strlen(name->valuestring); if (n >= sizeof(nickname)) { n = sizeof(nickname) - 1; while (n && ((unsigned char)name->valuestring[n] & 0xc0) == 0x80) --n; } memcpy(nickname, name->valuestring, n); }
        /* Malformed/server responses are not evidence that credentials expired. */
        if (cJSON_IsNull(profile)) err = NETEASE_DIRECT_ERR_AUTH;
        cJSON_Delete(root);
    }
    free(response);
    if (err == ESP_OK) err = netease_direct_session_save(session.cookie, uid, nickname);
    return err;
}

esp_err_t netease_direct_qr_start(netease_direct_qr_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    char *response = NULL; esp_err_t err = request("/api/login/qrcode/unikey", "{\"type\":3}", true, &response, NULL);
    if (err == ESP_OK) err = netease_direct_parse_qr_key(response, out);
    free(response); return err;
}

esp_err_t netease_direct_qr_poll(const char *key, netease_direct_qr_status_t *out)
{
    if (!key || !*key || strlen(key) >= NETEASE_DIRECT_QR_KEY_MAX || !out ||
        strspn(key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(key)) return ESP_ERR_INVALID_ARG;
    char json[192]; snprintf(json, sizeof(json), "{\"key\":\"%s\",\"type\":3}", key);
    char *response = NULL; esp_err_t err = request("/api/login/qrcode/client/login", json, true, &response, NULL);
    if (err == ESP_OK) err = netease_direct_parse_qr_status(response, out);
    if (err != ESP_OK) ESP_LOGW("netease_direct", "QR poll/parse failed: err=%d", err);
    free(response);
    if (err == ESP_OK && out->state == NETEASE_DIRECT_QR_CONFIRMED) {
        err = netease_direct_session_verify();
        if (err != ESP_OK) ESP_LOGW("netease_direct", "QR account verification failed: err=%d", err);
        if (err == ESP_OK) strcpy(out->nickname, session.nickname);
    }
    return err;
}

esp_err_t netease_direct_get_playlists(const char *cookie, netease_direct_playlist_list_t *out)
{
    (void)cookie; if (!out || session.user_id <= 0) return ESP_ERR_INVALID_STATE;
    char json[128]; snprintf(json, sizeof(json), "{\"uid\":%" PRId64 ",\"limit\":20,\"offset\":0}", session.user_id);
    direct_stream_t *s = malloc(sizeof(*s)); if (!s) return ESP_ERR_NO_MEM;
    direct_stream_init(s, DIRECT_STREAM_PLAYLISTS, out, 0, 20);
    esp_err_t err = request("/weapi/user/playlist", json, false, NULL, s);
    if (err != ESP_OK) memset(out, 0, sizeof(*out));
    free(s); return err;
}

esp_err_t netease_direct_get_tracks(const char *cookie, int64_t id, uint32_t limit, uint32_t offset, netease_direct_track_list_t *out)
{
    (void)cookie; if (!out || id <= 0 || !limit || limit > NETEASE_DIRECT_MAX_TRACKS || offset > UINT32_MAX - limit) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    /* Only trackIds are needed here; current-page metadata comes from song/detail. */
    char json[1024]; snprintf(json, sizeof(json), "{\"id\":%" PRId64 ",\"n\":0,\"s\":0}", id);
    direct_stream_t *s = calloc(1,sizeof(*s)); if (!s) return ESP_ERR_NO_MEM;
    esp_err_t err=ESP_OK;
    uint32_t total=0;size_t count=0;
    if(id_cache.ids && id_cache.playlist==id && id_cache.user==session.user_id &&
       offset>=id_cache.start && (offset>=id_cache.total ||
       (offset-id_cache.start<id_cache.count && (limit<=id_cache.count-(offset-id_cache.start) ||
        id_cache.start+id_cache.count==id_cache.total))) &&
       time(NULL)-id_cache.loaded_at<300) {
        total=id_cache.total;
        count=offset<total?total-offset:0;
        if(count>limit)count=limit;
        if(count)memcpy(s->ids,id_cache.ids+offset-id_cache.start,count*sizeof(int64_t));
    } else {
        clear_id_cache();
        int64_t *ids=malloc(PLAYLIST_ID_CACHE_CAP*sizeof(int64_t));
        direct_stream_init(s,DIRECT_STREAM_IDS,NULL,offset,limit);
        uint32_t window_start=offset>128?offset-128:0;
        s->all_ids=ids;s->all_ids_capacity=ids?PLAYLIST_ID_CACHE_CAP:0;s->all_ids_offset=window_start;
        err=request("/weapi/v6/playlist/detail",json,false,NULL,s);
        total=s->total;count=s->id_count;
        if(err==ESP_OK && ids) {
            uint32_t retained=total>window_start?total-window_start:0;
            if(retained>PLAYLIST_ID_CACHE_CAP)retained=PLAYLIST_ID_CACHE_CAP;
            if(retained) { int64_t *compact=realloc(ids,retained*sizeof(*ids));if(compact)ids=compact; }
            id_cache.ids=ids;id_cache.playlist=id;id_cache.user=session.user_id;id_cache.total=total;
            id_cache.start=window_start;id_cache.count=retained;id_cache.loaded_at=time(NULL);
        } else {
            /* Keep no partial cache: offsets beyond the retained range would otherwise look valid. */
            free(ids);
        }
    }
    if (err == ESP_OK && count) {
        size_t used = (size_t)snprintf(json, sizeof(json), "{\"c\":\"[");
        for (size_t i = 0; i < count && used < sizeof(json); ++i) {
            int n = snprintf(json + used, sizeof(json) - used, "%s{\\\"id\\\":%" PRId64 "}", i ? "," : "", s->ids[i]);
            if (n < 0 || (size_t)n >= sizeof(json) - used) { err = ESP_ERR_INVALID_SIZE; break; }
            used += (size_t)n;
        }
        if (err == ESP_OK && used < sizeof(json)) snprintf(json + used, sizeof(json) - used, "]\"}");
        else err = ESP_ERR_INVALID_SIZE;
        int64_t ids[NETEASE_DIRECT_MAX_TRACKS]; memcpy(ids, s->ids, sizeof(ids));
        if (err == ESP_OK) {
            direct_stream_init(s, DIRECT_STREAM_SONGS, NULL, 0, limit); s->tracks = out;
            err = request("/weapi/v3/song/detail", json, false, NULL, s);
        }
        if (err == ESP_OK && out->count != count) err = ESP_ERR_INVALID_RESPONSE;
        for (size_t i = 0; err == ESP_OK && i < count; ++i) if (out->items[i].id != ids[i]) err = ESP_ERR_INVALID_RESPONSE;
    }
    free(s);
    if (err != ESP_OK) { memset(out, 0, sizeof(*out)); return err; }
    out->playlist_id = id; out->has_next_offset = offset + count < total; out->next_offset = offset + (uint32_t)count; return ESP_OK;
}

esp_err_t netease_direct_get_track_url(const char *cookie, int64_t id, int bitrate, netease_direct_track_url_t *out)
{
    (void)cookie; if (!out || id <= 0 || bitrate != 128000) return ESP_ERR_INVALID_ARG;
    char json[256]; snprintf(json, sizeof(json), "{\"ids\":\"[%" PRId64 "]\",\"br\":%d,\"encodeType\":\"mp3\",\"header\":{\"os\":\"pc\",\"appver\":\"2.9.7\"}}", id, bitrate);
    char *response = NULL; esp_err_t err = request("/api/song/enhance/player/url", json, true, &response, NULL);
    if (err == ESP_OK) err = netease_direct_parse_track_url(response, out);
    free(response);
    if (err == ESP_OK && out->track_id != id) { memset(out, 0, sizeof(*out)); err = ESP_ERR_INVALID_RESPONSE; }
    return err;
}

esp_err_t netease_direct_get_cover_url(int64_t id, char *out, size_t capacity)
{
    if(id<=0 || !out || !capacity)return ESP_ERR_INVALID_ARG;
    out[0]=0;
    direct_stream_t *s=calloc(1,sizeof(*s));
    netease_direct_track_list_t *tracks=calloc(1,sizeof(*tracks));
    if(!s || !tracks) { free(s);free(tracks);return ESP_ERR_NO_MEM; }
    direct_stream_init(s,DIRECT_STREAM_SONGS,NULL,0,1);s->tracks=tracks;
    char json[128];snprintf(json,sizeof(json),"{\"c\":\"[{\\\"id\\\":%" PRId64 "}]\"}",id);
    esp_err_t err=request("/weapi/v3/song/detail",json,false,NULL,s);
    if(err==ESP_OK && (tracks->count!=1 || tracks->items[0].id!=id))err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK && !s->cover_url[0])err=ESP_ERR_NOT_FOUND;
    if(err==ESP_OK && strlen(s->cover_url)>=capacity)err=ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK)strcpy(out,s->cover_url);
    free(s);free(tracks);return err;
}

esp_err_t netease_direct_get_lyrics(int64_t id, char *out, size_t capacity)
{
    if(id<=0 || !out || !capacity)return ESP_ERR_INVALID_ARG;
    out[0]=0;char json[128];
    snprintf(json,sizeof(json),"{\"id\":%" PRId64 ",\"lv\":-1,\"kv\":-1,\"tv\":-1}",id);
    char *response=NULL;esp_err_t err=request("/weapi/song/lyric",json,false,&response,NULL);
    if(err==ESP_OK) {
        cJSON *root=cJSON_Parse(response);
        cJSON *lrc=cJSON_GetObjectItemCaseSensitive(root,"lrc");
        cJSON *text=cJSON_GetObjectItemCaseSensitive(lrc,"lyric");
        if(!cJSON_IsString(text) || !text->valuestring[0])err=ESP_ERR_NOT_FOUND;
        else if(strlen(text->valuestring)>=capacity)err=ESP_ERR_INVALID_SIZE;
        else strcpy(out,text->valuestring);
        cJSON_Delete(root);
    }
    free(response);return err;
}
