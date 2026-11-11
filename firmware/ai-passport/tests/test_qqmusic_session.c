/* HTTP/NVS替身仅链接本测试，不接入生产组件。 */
#include "qqmusic_client.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "cJSON.h"
#include "bounded_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char stored[QQMUSIC_SESSION_MAX+1],pending[QQMUSIC_SESSION_MAX+1];
static bool fail_commit,fail_network;
static unsigned http_calls,erase_calls;
static const char *reply="{\"code\":0,\"req_0\":{\"code\":0,\"data\":{}}}";
struct fake_http { size_t read,written,expected;char body[8192]; };
static struct fake_http connection;
static bool http_active;
static bool lyrics_request,read_error,incomplete;
static int http_status=200;
static int64_t header_length;
static int64_t now_us,read_delay_us;
static bool guard_active,guard_stopped;
static unsigned guard_starts,guard_stops;
static esp_err_t guard_result;
esp_err_t bounded_http_event_handler(esp_http_client_event_t *event) { (void)event;return ESP_OK; }
esp_err_t bounded_http_open(esp_http_client_handle_t http,int64_t deadline)
{ assert(deadline==now_us+INT64_C(10000000));return esp_http_client_open(http,0); }
esp_err_t bounded_http_start(esp_http_client_handle_t http,int64_t deadline)
{
    assert(http==&connection && http_active && deadline>now_us && !guard_active);
    ++guard_starts;if(guard_result)return guard_result;
    guard_active=true;return ESP_OK;
}
bool bounded_http_expired(esp_http_client_handle_t http) { assert(http==&connection);return false; }
esp_err_t bounded_http_make_blocking(esp_http_client_handle_t http)
{ assert(http==&connection && http_active);return ESP_OK; }
void bounded_http_stop(esp_http_client_handle_t http)
{ assert(http==&connection && http_active);++guard_stops;guard_active=false;guard_stopped=true; }
int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t handle,int timeout_ms)
{ (void)handle;assert(timeout_ms>0 && timeout_ms<=10000);return ESP_OK; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config;return ESP_OK; }
uint32_t esp_random(void) { return 123456789; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle)
{
    (void)mode;assert(!strcmp(name,"qqmusic"));*handle=1;strcpy(pending,stored);return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle,const char *key,char *value,size_t *length)
{
    (void)handle;assert(!strcmp(key,"session"));if(!stored[0]) return ESP_ERR_NVS_NOT_FOUND;
    size_t required=strlen(stored)+1;
    if(value && *length<required) { *length=required;return ESP_ERR_NVS_INVALID_LENGTH; }
    if(value) memcpy(value,stored,required);
    *length=required;return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle,const char *key,const char *value)
{
    (void)handle;assert(!strcmp(key,"session"));assert(strlen(value)<sizeof(pending));strcpy(pending,value);return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle,const char *key)
{
    (void)handle;assert(!strcmp(key,"session"));++erase_calls;pending[0]=0;return stored[0]?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle;if(fail_commit) return ESP_FAIL;strcpy(stored,pending);return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(!http_active && config->disable_auto_redirect);
    lyrics_request=config->method==HTTP_METHOD_GET;
    if(lyrics_request) {
        assert(!strcmp(config->url,"https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=0039MnYb0qxYhV&format=json&nobase64=1"));
        assert(config->is_async && config->event_handler==bounded_http_event_handler);
    }
    else assert(!strcmp(config->url,"https://u.y.qq.com/cgi-bin/musicu.fcg"));
    memset(&connection,0,sizeof(connection));http_active=true;guard_stopped=false;++http_calls;return &connection;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle,const char *name,const char *value)
{
    (void)handle;(void)value;
    if(lyrics_request)assert(!strcmp(name,"Referer") || !strcmp(name,"User-Agent"));
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t handle,int length)
{
    handle->expected=(size_t)length;return fail_network?ESP_FAIL:ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t handle,const char *data,int length)
{
    int n=length>17?17:length;assert(handle->written+(size_t)n<sizeof(handle->body));
    memcpy(handle->body+handle->written,data,(size_t)n);handle->written+=(size_t)n;return n;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle)
{
    assert(handle->written==handle->expected);handle->body[handle->written]=0;
    if(lyrics_request)return header_length;
    cJSON *root=cJSON_Parse(handle->body);assert(root);
    cJSON *comm=cJSON_GetObjectItemCaseSensitive(root,"comm");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(comm,"ct")->valuestring,"11"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(comm,"authst")->valuestring,"isolated-A"));
    cJSON_Delete(root);return 0; /* 无Content-Length的分片响应。 */
}
int esp_http_client_get_status_code(esp_http_client_handle_t handle) { (void)handle;return http_status; }
int esp_http_client_read(esp_http_client_handle_t handle,char *data,int length)
{
    now_us+=read_delay_us;
    if(read_error)return -1;
    size_t n=strlen(reply)-handle->read;if(n>(size_t)length)n=(size_t)length;if(n>13)n=13;
    memcpy(data,reply+handle->read,n);handle->read+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle) { return !incomplete && handle->read==strlen(reply); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle)
{ assert(handle==&connection && http_active && !guard_active);if(lyrics_request)assert(guard_stopped);http_active=false;return ESP_OK; }
int main(void)
{
    const char *a="{\"musicid\":123,\"musickey\":\"isolated-A\",\"loginType\":6,\"refresh_key\":\"local\",\"expired_at\":0}";
    const char *b="{\"musicid\":124,\"musickey\":\"isolated-B\",\"loginType\":6}";
    qqmusic_client_t client;qqmusic_client_init(&client);
    assert(qqmusic_client_accept(&client,a)==ESP_OK && !strcmp(client.account,"123"));
    fail_commit=true;
    assert(qqmusic_client_accept(&client,b)!=ESP_OK && !strcmp(client.account,"123") && !strcmp(stored,a));
    fail_commit=false;qqmusic_client_close(&client);
    assert(qqmusic_client_restore(&client)==ESP_OK && !client.blocked && !http_active);
    fail_network=true;
    assert(qqmusic_client_restore(&client)==ESP_FAIL && client.blocked && !strcmp(stored,a) && !erase_calls);
    assert(!http_active);fail_network=false;
    assert(qqmusic_client_restore(&client)==ESP_OK);
    qqmusic_track_t track={0};strcpy(track.mid,"0039MnYb0qxYhV");char lyrics[128];
    reply="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":\"[00:01.00]测试\\n\"}";
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_OK && !strcmp(lyrics,"[00:01.00]测试\n") && !http_active);
    assert(guard_starts==1 && guard_stops==1);
    guard_result=ESP_ERR_INVALID_STATE;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_STATE && !lyrics[0] && !http_active);guard_result=ESP_OK;
    fail_network=true;strcpy(lyrics,"old");
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_FAIL && !lyrics[0] && !http_active);fail_network=false;
    http_status=302;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_RESPONSE && !lyrics[0] && !http_active);http_status=200;
    http_status=404;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_NOT_FOUND && !lyrics[0] && !http_active);
    http_status=403;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==QQMUSIC_ERR_UNAVAILABLE && !lyrics[0] && !http_active);http_status=200;
    header_length=QQMUSIC_LYRICS_RESPONSE_MAX+1;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_SIZE && !lyrics[0] && !http_active);header_length=0;
    read_error=true;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_FAIL && !lyrics[0] && !http_active);read_error=false;
    incomplete=true;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_RESPONSE && !lyrics[0] && !http_active);incomplete=false;
    read_delay_us=INT64_C(10000001);
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_TIMEOUT && !lyrics[0] && !http_active);
    read_error=true;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_TIMEOUT && !lyrics[0] && !http_active);read_error=false;read_delay_us=0;
    char oversized[QQMUSIC_LYRICS_RESPONSE_MAX+2];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;reply=oversized;
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_SIZE && !lyrics[0] && !http_active);
    reply="not JSON";
    assert(qqmusic_client_lyrics(&client,&track,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_RESPONSE && !lyrics[0] && !http_active);
    assert(guard_stops==guard_starts+1); /* 只有open失败未arm，仍须在cleanup前disarm。 */
    reply="{\"code\":0,\"req_0\":{\"code\":104401}}";
    assert(qqmusic_client_restore(&client)==QQMUSIC_ERR_AUTH && client.blocked && !erase_calls && !strcmp(stored,a));
    reply="{\"code\":0,\"req_0\":{\"code\":0,\"data\":{}}}";
    assert(qqmusic_client_restore(&client)==ESP_OK);
    reply="{\"code\":0,\"req_0\":{\"code\":0,\"data\":{\"musicid\":124,\"musickey\":\"isolated-B\",\"loginType\":6}}}";
    assert(qqmusic_client_refresh(&client)==ESP_ERR_INVALID_RESPONSE && !strcmp(client.account,"123") && !strcmp(stored,a));
    fail_commit=true;assert(qqmusic_client_logout(&client)==ESP_FAIL && client.blocked && !client.credential && !strcmp(stored,a));
    unsigned before=http_calls;
    assert(qqmusic_client_verify(&client)==ESP_ERR_INVALID_STATE && http_calls==before);
    fail_commit=false;assert(qqmusic_client_logout(&client)==ESP_OK && !stored[0]);
    qqmusic_client_close(&client);puts("qqmusic_session: 6 groups passed");return 0;
}
