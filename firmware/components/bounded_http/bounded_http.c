#include "bounded_http.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stddef.h>
#include <limits.h>
#include <fcntl.h>

/* 配置事件处理器在open调用线程内同步通知；不使用HTTP对象私有状态或凭据。 */
static esp_http_client_handle_t s_open_owner;
static bool s_open_connected;

esp_err_t bounded_http_event_handler(esp_http_client_event_t *event)
{
    if(event && event->client==s_open_owner && event->event_id==HTTP_EVENT_ON_CONNECTED)s_open_connected=true;
    return ESP_OK;
}

esp_err_t bounded_http_open(esp_http_client_handle_t http,int64_t absolute_deadline_us)
{
    if(!http)return ESP_ERR_INVALID_ARG;
    if(s_open_owner)return ESP_ERR_INVALID_STATE;
    s_open_owner=http;s_open_connected=false;esp_err_t err;
    for(;;) {
        int64_t remaining=absolute_deadline_us-esp_timer_get_time();
        if(remaining<=0) { err=ESP_ERR_TIMEOUT;break; }
        int64_t timeout_ms=(remaining+999)/1000;
        err=esp_http_client_set_timeout_ms(http,timeout_ms>INT_MAX?INT_MAX:(int)timeout_ms);
        if(err!=ESP_OK)break;
        err=esp_http_client_open(http,0);
        if(esp_timer_get_time()>=absolute_deadline_us) { err=ESP_ERR_TIMEOUT;break; }
        if((err!=ESP_ERR_HTTP_CONNECTING && err!=ESP_ERR_HTTP_EAGAIN) || s_open_connected)break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_open_owner=NULL;s_open_connected=false;
    return err;
}

esp_err_t bounded_http_make_blocking(esp_http_client_handle_t http)
{
    int fd=http?esp_http_client_get_socket(http):-1;
    if(fd<0)return ESP_ERR_INVALID_STATE;
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags & ~O_NONBLOCK)<0)return ESP_FAIL;
    return ESP_OK;
}

/* 初始化与start/stop只允许来自唯一联网worker；timer task只访问锁保护的字段。 */
static struct {
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
    esp_timer_handle_t timer;
    esp_http_client_handle_t owner;
    int fd;
    int64_t deadline;
    bool active,expired;
} s_guard;

static void deadline_reached(void *arg)
{
    (void)arg;
    /* 锁只保护几个字段和本地socket shutdown，绝不覆盖HTTP/TLS或timer stop。 */
    xSemaphoreTake(s_guard.lock,portMAX_DELAY);
    if(s_guard.active && esp_timer_get_time()>=s_guard.deadline) {
        s_guard.active=false;s_guard.expired=true;
        shutdown(s_guard.fd,SHUT_RD);
    }
    xSemaphoreGive(s_guard.lock);
}

esp_err_t bounded_http_start(esp_http_client_handle_t http,int64_t absolute_deadline_us)
{
    if(!http)return ESP_ERR_INVALID_ARG;
    if(!s_guard.lock) {
        s_guard.lock=xSemaphoreCreateMutexStatic(&s_guard.lock_storage);
        if(!s_guard.lock)return ESP_ERR_NO_MEM;
    }
    if(!s_guard.timer) {
        const esp_timer_create_args_t config={.callback=deadline_reached,.name="http_deadline"};
        esp_err_t err=esp_timer_create(&config,&s_guard.timer);
        if(err!=ESP_OK)return err;
    }
    xSemaphoreTake(s_guard.lock,portMAX_DELAY);
    esp_err_t err=ESP_OK;
    if(s_guard.owner)err=ESP_ERR_INVALID_STATE;
    else {
        int fd=esp_http_client_get_socket(http);
        int64_t remaining=absolute_deadline_us-esp_timer_get_time();
        if(fd<0)err=ESP_ERR_INVALID_STATE;
        else if(remaining<=0)err=ESP_ERR_TIMEOUT;
        else {
            s_guard.owner=http;s_guard.fd=fd;s_guard.deadline=absolute_deadline_us;
            s_guard.expired=false;s_guard.active=true;
            err=esp_timer_start_once(s_guard.timer,(uint64_t)remaining);
            if(err!=ESP_OK) { s_guard.owner=NULL;s_guard.fd=-1;s_guard.active=false; }
        }
    }
    xSemaphoreGive(s_guard.lock);
    return err;
}

bool bounded_http_expired(esp_http_client_handle_t http)
{
    if(!http || !s_guard.lock)return false;
    xSemaphoreTake(s_guard.lock,portMAX_DELAY);
    bool expired=s_guard.owner==http && (s_guard.expired || esp_timer_get_time()>=s_guard.deadline);
    xSemaphoreGive(s_guard.lock);
    return expired;
}

void bounded_http_stop(esp_http_client_handle_t http)
{
    if(!http || !s_guard.lock)return;
    xSemaphoreTake(s_guard.lock,portMAX_DELAY);
    bool owner=s_guard.owner==http;
    if(owner) {
        s_guard.active=false;s_guard.owner=NULL;s_guard.fd=-1;s_guard.deadline=0;
    }
    xSemaphoreGive(s_guard.lock);
    /* SDK stop不等待已派发的callback；解除active后，即使迟到也只看到静态上下文。 */
    if(owner)esp_timer_stop(s_guard.timer);
}
