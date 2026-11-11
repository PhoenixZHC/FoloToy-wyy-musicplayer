/* 运行真实guard实现，socket/timer/RTOS替身仅链接本主机入口。 */
#include "bounded_http.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <assert.h>
#include <stdio.h>

struct test_http { int fd;bool cleaned; };
struct test_timer { void (*callback)(void *);bool armed; };
static struct test_timer s_timer;
static SemaphoreHandle_t lock;
static int64_t now_us;
static unsigned created,stopped,shutdowns;
static int shutdown_fd;
static esp_err_t create_result,start_result;
static esp_err_t open_result,timeout_result;
static unsigned connecting_left,open_calls,yields;
static bool report_connected;
void vTaskDelay(unsigned ticks) { assert(ticks==20);now_us+=(int64_t)ticks*1000;++yields; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t http,int timeout_ms)
{ assert(http && timeout_ms>0);return timeout_result; }
esp_err_t esp_http_client_open(esp_http_client_handle_t http,int write_len)
{
    assert(!http->cleaned && write_len==0);++open_calls;
    if(connecting_left) { --connecting_left;return ESP_ERR_HTTP_EAGAIN; }
    if(report_connected) { esp_http_client_event_t event={.client=http,.event_id=HTTP_EVENT_ON_CONNECTED};bounded_http_event_handler(&event); }
    return open_result;
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) { lock=storage;return storage; }
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned ticks)
{ (void)ticks;assert(mutex==lock && !mutex->held);mutex->held=true;return 1; }
int xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex==lock && mutex->held);mutex->held=false;return 1; }
int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *config,esp_timer_handle_t *out)
{
    if(create_result)return create_result;
    ++created;s_timer.callback=config->callback;*out=&s_timer;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t delay_us)
{ assert(timer==&s_timer && delay_us>0 && !timer->armed);if(start_result)return start_result;timer->armed=true;return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{ assert(timer==&s_timer && !lock->held);++stopped;bool armed=timer->armed;timer->armed=false;return armed?ESP_OK:ESP_ERR_INVALID_STATE; }
int esp_http_client_get_socket(esp_http_client_handle_t http) { assert(!http->cleaned);return http->fd; }
int shutdown(int fd,int how)
{ assert(lock->held && how==SHUT_RD);++shutdowns;shutdown_fd=fd;return 0; }
static void callback(bool real_expiry)
{ if(real_expiry)s_timer.armed=false;s_timer.callback(NULL); }
static void cleanup(esp_http_client_handle_t http)
{ bounded_http_stop(http);http->cleaned=true;assert(!lock->held); }
int main(void)
{
    struct test_http a={.fd=7},b={.fd=8},bad={.fd=-1};
    assert(bounded_http_start(NULL,100)==ESP_ERR_INVALID_ARG);
    create_result=ESP_ERR_NO_MEM;assert(bounded_http_start(&a,100)==ESP_ERR_NO_MEM);create_result=ESP_OK;
    assert(bounded_http_start(&bad,100)==ESP_ERR_INVALID_STATE);
    assert(bounded_http_start(&a,0)==ESP_ERR_TIMEOUT);
    start_result=ESP_ERR_INVALID_STATE;assert(bounded_http_start(&a,100)==ESP_ERR_INVALID_STATE);start_result=ESP_OK;
    assert(bounded_http_start(&a,100)==ESP_OK && created==1);
    assert(bounded_http_start(&b,200)==ESP_ERR_INVALID_STATE);
    bounded_http_stop(&b);assert(!stopped); /* 非owner不能解除其他请求。 */
    now_us=99;callback(false);assert(!shutdowns && !bounded_http_expired(&a));
    now_us=100;callback(true);assert(shutdowns==1 && shutdown_fd==7 && bounded_http_expired(&a));
    callback(false);assert(shutdowns==1); /* 重复/迟到回调不重复shutdown。 */
    assert(bounded_http_start(&b,200)==ESP_ERR_INVALID_STATE);cleanup(&a);
    callback(false);assert(shutdowns==1); /* cleanup之后没有TLS/handle访问。 */
    b.fd=7;assert(bounded_http_start(&b,200)==ESP_OK); /* 操作系统复用旧fd。 */
    callback(false);assert(shutdowns==1 && !bounded_http_expired(&b));
    now_us=199;callback(false);assert(shutdowns==1);
    now_us=200;callback(true);assert(shutdowns==2 && shutdown_fd==7);
    cleanup(&b);callback(false);assert(shutdowns==2 && stopped==2 && created==1);
    b.cleaned=false;assert(bounded_http_start(&b,300)==ESP_OK);cleanup(&b);
    now_us=400;callback(false);assert(shutdowns==2 && stopped==3);
    b.cleaned=false;now_us=0;connecting_left=2;report_connected=true;
    assert(bounded_http_open(&b,100000)==ESP_OK && open_calls==3 && yields==2);
    now_us=0;connecting_left=10;unsigned calls=open_calls;
    assert(bounded_http_open(&b,30000)==ESP_ERR_TIMEOUT && open_calls==calls+2);
    connecting_left=0;open_result=ESP_ERR_INVALID_STATE;calls=open_calls;
    assert(bounded_http_open(&b,100000)==ESP_ERR_INVALID_STATE && open_calls==calls+1);
    open_result=ESP_ERR_HTTP_EAGAIN;calls=open_calls;
    assert(bounded_http_open(&b,100000)==ESP_ERR_HTTP_EAGAIN && open_calls==calls+1); /* 部分headers写不重试。 */
    report_connected=false;open_result=ESP_ERR_HTTP_CONNECTING;now_us=0;
    assert(bounded_http_open(&b,20000)==ESP_ERR_TIMEOUT);
    timeout_result=ESP_ERR_INVALID_ARG;calls=open_calls;
    assert(bounded_http_open(&b,100000)==ESP_ERR_INVALID_ARG && open_calls==calls);
    assert(bounded_http_open(NULL,100000)==ESP_ERR_INVALID_ARG);
    assert(bounded_http_open(&b,now_us)==ESP_ERR_TIMEOUT);
    puts("bounded_http: 7 groups passed");return 0;
}
