/* HTTP/NVS替身仅链接本测试，不接入生产组件。 */
#include "qqmusic_client.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "cJSON.h"
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
    assert(!strcmp(config->url,"https://u.y.qq.com/cgi-bin/musicu.fcg"));
    memset(&connection,0,sizeof(connection));http_active=true;++http_calls;return &connection;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle,const char *name,const char *value)
{
    (void)handle;(void)name;(void)value;return ESP_OK;
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
    cJSON *root=cJSON_Parse(handle->body);assert(root);
    cJSON *comm=cJSON_GetObjectItemCaseSensitive(root,"comm");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(comm,"ct")->valuestring,"11"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(comm,"authst")->valuestring,"isolated-A"));
    cJSON_Delete(root);return 0; /* 无Content-Length的分片响应。 */
}
int esp_http_client_get_status_code(esp_http_client_handle_t handle) { (void)handle;return 200; }
int esp_http_client_read(esp_http_client_handle_t handle,char *data,int length)
{
    size_t n=strlen(reply)-handle->read;if(n>(size_t)length)n=(size_t)length;if(n>13)n=13;
    memcpy(data,reply+handle->read,n);handle->read+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle) { return handle->read==strlen(reply); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) { assert(handle==&connection && http_active);http_active=false;return ESP_OK; }
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
    qqmusic_client_close(&client);puts("qqmusic_session: 5 groups passed");return 0;
}
