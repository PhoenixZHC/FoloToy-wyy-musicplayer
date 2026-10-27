/* Isolated in-memory HTTP/NVS fakes; never compiled into firmware. */
#define ESP_PLATFORM
#define CONFIG_MBEDTLS_DYNAMIC_BUFFER 1
#define CONFIG_ESP_TLS_DYN_BUF_STRATEGY_SUPPORTED 1
#include "../../components/netease_direct_client/direct_http.c"
#include <assert.h>
#include "mbedtls/aes.h"

struct fake_http { esp_http_client_config_t config; };
static struct fake_http fake;
static unsigned calls, erases, commits;
static unsigned connections;
static int http_status = 200;
static esp_err_t transport_error, commit_error;
static unsigned fail_headers;
static esp_err_t fail_headers_error = ESP_ERR_HTTP_FETCH_HEADER;
static bool fail_after_data;
static const char *response_json, *set_cookie;
static bool playlist_fixture;
static bool media_fixture;
static unsigned paged_start;
static unsigned paged_total=21;
static char paged_json[4096];
static netease_direct_session_t stored, pending;
static bool stored_exists;
static char last_url[192], last_cookie[BODY_CAP], last_payload[8192];

esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    ++connections;
    assert(config->disable_auto_redirect && config->crt_bundle_attach);
    assert(strncmp(config->url, "https://", 8) == 0);
    assert(config->buffer_size_tx >= NETEASE_DIRECT_COOKIE_MAX_BYTES + 512);
    assert(config->tls_dyn_buf_strategy == (strstr(config->url, "/eapi/") ? 0 : HTTP_TLS_DYN_BUF_RX_STATIC));
    strcpy(last_url, config->url); last_cookie[0] = 0; fake.config = *config; return &fake;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *value)
{ (void)h; if (strcmp(key, "Cookie") == 0) strcpy(last_cookie, value); return ESP_OK; }
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t h, const char *body, int length)
{
    if(!body) { assert(length==0);return ESP_OK; }
    (void)h; assert(length > 7 && strncmp(body, "params=", 7) == 0);
    last_payload[0] = 0;
    if (strstr(last_url, "/eapi/")) {
        size_t n = ((size_t)length - 7) / 2;
        unsigned char encrypted[8192], plain[8193];
        assert(n < sizeof(encrypted) && n % 16 == 0);
        for (size_t i = 0; i < n; ++i) { unsigned byte; assert(sscanf(body + 7 + i * 2, "%2x", &byte) == 1); encrypted[i] = (unsigned char)byte; }
        mbedtls_aes_context aes; mbedtls_aes_init(&aes);
        assert(mbedtls_aes_setkey_dec(&aes, (const unsigned char *)"e82ckenh8dichen8", 128) == 0);
        for (size_t i = 0; i < n; i += 16) assert(mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, encrypted + i, plain + i) == 0);
        mbedtls_aes_free(&aes);
        assert(plain[n - 1] > 0 && plain[n - 1] <= 16); plain[n - plain[n - 1]] = 0;
        char *start = strstr((char *)plain, "-36cd479b6b5-"); assert(start); start += 12;
        char *end = strstr(start, "-36cd479b6b5-"); assert(end); *end = 0; strcpy(last_payload, start);
    }
    return ESP_OK;
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t h)
{
    if(fail_headers) { --fail_headers;++calls;return fail_headers_error; }
    if(playlist_fixture)response_json=strstr(last_url,"playlist/detail")?
        "{\"code\":200,\"playlist\":{\"trackIds\":[{\"id\":9},{\"id\":10},{\"id\":11}]}}":
        "{\"code\":200,\"songs\":[{\"id\":9,\"name\":\"A\",\"dt\":100,\"ar\":[{\"name\":\"B\"}]}]}";
    if(media_fixture)response_json=strstr(last_url,"/song/detail")?
        "{\"code\":200,\"songs\":[{\"id\":9,\"name\":\"A\",\"dt\":100,\"ar\":[{\"name\":\"B\"}],\"al\":{\"picUrl\":\"https://p1.music.126.net/a.jpg\"}}]}":
        "{\"code\":200,\"lrc\":{\"version\":1,\"lyric\":\"[00:01.00]line\\n\"}}";
    if(paged_start && strstr(last_url,"playlist/detail")) {
        ++calls;
        esp_http_client_event_t e={.user_data=h->config.user_data,.event_id=HTTP_EVENT_ON_DATA};
        e.data="{\"code\":200,\"playlist\":{\"trackIds\":[";e.data_len=(int)strlen(e.data);
        esp_err_t err=h->config.event_handler(&e);if(err)return err;
        for(unsigned i=1;i<=paged_total;i++) {
            char item[40];snprintf(item,sizeof(item),"%s{\"id\":%u}",i==1?"":",",i);
            e.data=item;e.data_len=(int)strlen(item);err=h->config.event_handler(&e);if(err)return err;
        }
        e.data="]}}";e.data_len=3;return h->config.event_handler(&e);
    }
    if(paged_start) {
        size_t used=(size_t)snprintf(paged_json,sizeof(paged_json),"{\"code\":200,%s[",strstr(last_url,"playlist/detail")?"\"playlist\":{\"trackIds\":" : "\"songs\":");
        unsigned first=strstr(last_url,"playlist/detail")?1:paged_start;
        unsigned count=strstr(last_url,"playlist/detail")?21:8;
        if(first+count>paged_total+1)count=paged_total+1-first;
        for(unsigned i=0;i<count;i++)used+=(size_t)snprintf(paged_json+used,sizeof(paged_json)-used,"%s{\"id\":%u,\"name\":\"Song\",\"dt\":100,\"ar\":[{\"name\":\"Artist\"}]}",i?",":"",first+i);
        snprintf(paged_json+used,sizeof(paged_json)-used,strstr(last_url,"playlist/detail")?"]}}":"]}");
        response_json=paged_json;
    }
    ++calls; if (transport_error) return transport_error;
    esp_http_client_event_t e = {.user_data = h->config.user_data};
    if (set_cookie) { e.event_id = HTTP_EVENT_ON_HEADER; e.header_key = "Set-Cookie"; e.header_value = set_cookie; esp_err_t err = h->config.event_handler(&e); if (err) return err; }
    if (response_json) for (size_t i = 0; i < strlen(response_json); i += 7) {
        e.event_id = HTTP_EVENT_ON_DATA; e.data = response_json + i; size_t n = strlen(response_json) - i; e.data_len = (int)(n < 7 ? n : 7);
        esp_err_t err = h->config.event_handler(&e); if (err) return err;
        if(fail_after_data)return fail_headers_error;
    }
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) { (void)h; return http_status; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { (void)h; return ESP_OK; }
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t h,void *data)
{ h->config.user_data=data;return ESP_OK; }
esp_err_t esp_http_client_set_url(esp_http_client_handle_t h,const char *url)
{ (void)h;strcpy(last_url,url);return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{ assert(strcmp(name, "netease_session") == 0); *h = 1; return mode == NVS_READONLY && !stored_exists ? ESP_ERR_NVS_NOT_FOUND : ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{ (void)h; assert(strcmp(key, "v1") == 0 && *size >= sizeof(stored)); if (!stored_exists) return ESP_ERR_NVS_NOT_FOUND; memcpy(out, &stored, sizeof(stored)); *size = sizeof(stored); return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{ (void)h; assert(strcmp(key, "v1") == 0 && size == sizeof(pending)); memcpy(&pending, data, size); return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{ (void)h; assert(strcmp(key, "v1") == 0); ++erases; stored_exists = false; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h)
{ (void)h; ++commits; if (commit_error) return commit_error; stored = pending; stored_exists = true; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

int main(void)
{
    netease_direct_session_t loaded;
    const netease_direct_client_config_t config = {.timeout_ms = 15000};
    assert(netease_direct_client_init(&config) == ESP_OK);
    assert(netease_direct_session_load(&loaded) == ESP_ERR_NVS_NOT_FOUND);
    assert(netease_direct_session_save("MUSIC_U=old; __csrf=x", 123, "A") == ESP_OK);
    assert(netease_direct_session_load(&loaded) == ESP_OK && loaded.user_id == 123);
    transport_error = ESP_ERR_TIMEOUT;
    assert(netease_direct_session_verify() == ESP_ERR_TIMEOUT);
    assert(erases == 0 && stored.user_id == 123);
    transport_error = ESP_OK; response_json = "{\"code\":200,\"profile\":{}}";
    assert(netease_direct_session_verify() == ESP_ERR_INVALID_RESPONSE && erases == 0);
    response_json = "{\"code\":200,\"profile\":null}";
    assert(netease_direct_session_verify() == NETEASE_DIRECT_ERR_AUTH);
    http_status = 403;
    assert(netease_direct_session_verify() == ESP_ERR_INVALID_RESPONSE && erases == 0);
    http_status = 401;
    assert(netease_direct_session_verify() == NETEASE_DIRECT_ERR_AUTH && erases == 0);
    http_status = 200; response_json = "{\"code\":301}";
    assert(netease_direct_session_verify() == NETEASE_DIRECT_ERR_AUTH);
    response_json = "{\"code\":200,\"profile\":{\"userId\":123,\"nickname\":\"B\"}}";
    set_cookie = "MUSIC_U=new; Path=/; HttpOnly";
    assert(netease_direct_session_verify() == ESP_OK);
    assert(strstr(last_cookie, "MUSIC_U=old") && strstr(stored.cookie, "MUSIC_U=new"));
    assert(strcmp(stored.nickname, "B") == 0);
    commit_error = ESP_FAIL;
    assert(netease_direct_session_save("MUSIC_U=bad", 456, "C") == ESP_FAIL);
    assert(session.user_id == 123 && stored.user_id == 123); commit_error = ESP_OK;
    set_cookie = NULL; response_json = "{\"code\":200,\"unikey\":\"abc123\"}";
    netease_direct_qr_t key;
    assert(netease_direct_qr_start(&key) == ESP_OK);
    assert(strcmp(last_url, "https://interfacepc.music.163.com/eapi/login/qrcode/unikey") == 0);
    assert(strstr(last_payload, "\"type\":3") && strstr(last_payload, "\"e_r\":false"));
    assert(strstr(last_payload, "\"deviceId\":\"02:00:00:00:00:01\"") && strstr(last_payload, "\"requestId\":"));
    assert(strstr(last_cookie, "deviceId=02%3A00%3A00%3A00%3A00%3A01") && strstr(last_cookie, "MUSIC_U=new"));
    set_cookie = "NMTID=device-token; Path=/"; response_json = "{\"code\":801}";
    netease_direct_qr_status_t waiting;
    assert(netease_direct_qr_poll("abc123", &waiting) == ESP_OK);
    set_cookie = NULL;
    assert(netease_direct_qr_poll("abc123", &waiting) == ESP_OK);
    assert(strstr(last_payload, "\"NMTID\":\"device-token\"") && strstr(last_cookie, "NMTID=device-token"));
    set_cookie = NULL; response_json = "{\"code\":803}";
    netease_direct_qr_status_t qr;
    response_json = "{\"code\":8821}";
    assert(netease_direct_qr_poll("abc123", &qr) == NETEASE_DIRECT_ERR_LOGIN_REJECTED);
    assert(strcmp(last_url, "https://interfacepc.music.163.com/eapi/login/qrcode/client/login") == 0);
    assert(erases == 0 && stored.user_id == 123);
    response_json = "{\"code\":803}";
    assert(netease_direct_qr_poll("abc123", &qr) == ESP_ERR_INVALID_RESPONSE);
    response_json = "{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https://cdn.test/a.mp3\",\"expi\":900}]}";
    netease_direct_track_url_t url;
    assert(netease_direct_get_track_url(NULL, 9, 128000, &url) == ESP_OK);
    assert(strcmp(last_url, "https://interfacepc.music.163.com/eapi/song/enhance/player/url") == 0);
    media_fixture=true;char cover[256],lyrics[128];
    assert(netease_direct_get_cover_url(9,cover,sizeof(cover))==ESP_OK);
    assert(strcmp(cover,"https://p1.music.126.net/a.jpg")==0);
    assert(netease_direct_get_cover_url(9,cover,8)==ESP_ERR_INVALID_SIZE && cover[0]==0);
    assert(netease_direct_get_lyrics(9,lyrics,sizeof(lyrics))==ESP_OK);
    assert(strcmp(lyrics,"[00:01.00]line\n")==0);
    media_fixture=false;
    playlist_fixture=true;
    netease_direct_track_list_t tracks;unsigned before=calls;
    assert(netease_direct_get_tracks(NULL,100,1,0,&tracks)==ESP_OK && calls==before+2);
    assert(id_cache.ids && id_cache.total==3);
    before=calls;
    assert(netease_direct_get_tracks(NULL,100,1,0,&tracks)==ESP_OK && calls==before+1);
    before=calls;
    assert(netease_direct_get_tracks(NULL,100,1,3,&tracks)==ESP_OK && !tracks.count && calls==before);
    assert(netease_direct_get_tracks(NULL,100,1,1,&tracks)==ESP_ERR_INVALID_RESPONSE);
    id_cache.loaded_at=time(NULL)-301;before=calls;
    assert(netease_direct_get_tracks(NULL,100,1,0,&tracks)==ESP_OK && calls==before+2);
    clear_id_cache();assert(!id_cache.ids);
    playlist_fixture=false;
    for(unsigned offset=0;offset<21;offset+=8) {
        paged_start=offset+1;before=calls;
        assert(netease_direct_get_tracks(NULL,200,8,offset,&tracks)==ESP_OK);
        assert(calls==before+(offset?1:2));
        assert(tracks.items[0].id==offset+1 && tracks.count==(offset==16?5:8));
        assert(tracks.has_next_offset==(offset<16));
        assert(tracks.next_offset==(offset==16?21:offset+8));
    }
    paged_start=13;
    assert(netease_direct_get_tracks(NULL,200,8,12,&tracks)==ESP_OK && tracks.items[0].id==13 && tracks.count==8);
    paged_total=4097;paged_start=3501;clear_id_cache();before=calls;
    assert(netease_direct_get_tracks(NULL,200,8,3500,&tracks)==ESP_OK && tracks.items[0].id==3501);
    assert(id_cache.total==4097 && id_cache.count==512 && id_cache.start==3372);
    paged_start=3509;before=calls;
    assert(netease_direct_get_tracks(NULL,200,8,3508,&tracks)==ESP_OK && tracks.items[0].id==3509 && calls==before+1);
    paged_start=1;before=calls;
    assert(netease_direct_get_tracks(NULL,200,8,0,&tracks)==ESP_OK && tracks.items[0].id==1 && calls==before+2);
    assert(id_cache.start==0 && id_cache.count==512);
    paged_start=0;clear_id_cache();
    char *huge = malloc(RESPONSE_CAP + 20); assert(huge); memset(huge, 'x', RESPONSE_CAP + 19); huge[RESPONSE_CAP + 19] = 0; response_json = huge;
    assert(netease_direct_get_track_url(NULL, 9, 128000, &url) == ESP_ERR_INVALID_SIZE); free(huge);
    assert(erases == 0 && calls > 10 && commits > 1);
    release_http();response_json="{\"code\":200}";set_cookie=NULL;transport_error=ESP_OK;http_status=200;
    unsigned opened=connections;
    assert(request("/weapi/test","{}",false,NULL,NULL)==ESP_OK);
    assert(request("/weapi/test2","{}",false,NULL,NULL)==ESP_OK);
    assert(connections==opened+1 && reusable_http && !fake.config.user_data);
    transport_error=ESP_FAIL;
    assert(request("/weapi/test3","{}",false,NULL,NULL)==ESP_FAIL && !reusable_http);
    transport_error=ESP_OK;
    assert(request("/weapi/test4","{}",false,NULL,NULL)==ESP_OK && connections==opened+2);
    opened=connections;before=calls;fail_headers=1;
    netease_direct_playlist_list_t playlists;
    response_json="{\"code\":200,\"playlist\":[]}";
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_OK);
    assert(calls==before+2 && connections==opened+1);
    before=calls;opened=connections;fail_headers=1;fail_headers_error=ESP_ERR_HTTP_WRITE_DATA;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_OK);
    assert(calls==before+2 && connections==opened+1);
    fail_headers_error=ESP_ERR_HTTP_FETCH_HEADER;
    fail_after_data=true;before=calls;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_ERR_HTTP_FETCH_HEADER && calls==before+1);
    fail_after_data=false;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_OK);
    fail_after_data=true;fail_headers_error=ESP_ERR_HTTP_WRITE_DATA;before=calls;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_ERR_HTTP_WRITE_DATA && calls==before+1);
    fail_headers_error=ESP_ERR_HTTP_FETCH_HEADER;
    fail_after_data=false;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_OK);
    before=calls;fail_headers=3;
    assert(netease_direct_get_playlists(NULL,&playlists)==ESP_ERR_HTTP_FETCH_HEADER);
    assert(calls==before+2 && fail_headers==1 && !reusable_http);
    fail_headers=1;before=calls;
    assert(netease_direct_get_track_url(NULL,9,128000,&url)==ESP_ERR_HTTP_FETCH_HEADER && calls==before+1);
    fail_headers=1;fail_headers_error=ESP_ERR_HTTP_WRITE_DATA;before=calls;
    assert(netease_direct_get_track_url(NULL,9,128000,&url)==ESP_ERR_HTTP_WRITE_DATA && calls==before+1);
    release_http();
    puts("netease_direct_http: PASS"); return 0;
}
