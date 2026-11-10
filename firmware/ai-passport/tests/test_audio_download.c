#include "audio_download_stubs.h"
#include "audio_download.h"
#include "audio_stream_policy.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum scenario { NORMAL, TIMEOUT, DISCONNECT, EARLY_EOF, IGNORE_RANGE, BAD_RANGE,
                TRUNCATED, CHUNKED, CANCEL_FULL, CHANGED_ETAG };
static enum scenario s_scenario;
static atomic_uint s_opens, s_tasks, s_clients;
static unsigned s_timeouts;
static bool s_broken;
static const size_t s_length = 40000;
struct test_stream {
    pthread_mutex_t lock;
    pthread_cond_t space;
    unsigned char *data;
    size_t capacity, read, count;
};
struct test_http {
    esp_http_client_config_t config;
    size_t position, start;
    bool range;
};
static void sleep_us(long us)
{
    struct timespec ts = {.tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
void esp_crt_bundle_attach(void) {}
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 50000; }
int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000) * 10;
}
void vTaskDelay(unsigned ticks) { sleep_us((long)ticks * 100); }
struct task_entry { void (*entry)(void *); void *arg; };
static void *run_task(void *value)
{
    struct task_entry task = *(struct task_entry *)value;
    free(value);
    task.entry(task.arg);
    return NULL;
}
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    struct task_entry *task = malloc(sizeof(*task)); assert(task);
    task->entry = entry; task->arg = arg;
    pthread_t thread;
    atomic_fetch_add(&s_tasks, 1);
    assert(pthread_create(&thread, NULL, run_task, task) == 0);
    assert(pthread_detach(thread) == 0);
    return pdPASS;
}
void vTaskDelete(void *handle)
{
    (void)handle;
    atomic_fetch_sub(&s_tasks, 1);
    pthread_exit(NULL);
}
StreamBufferHandle_t xStreamBufferCreate(size_t capacity, size_t trigger)
{
    (void)trigger;
    struct test_stream *s = calloc(1, sizeof(*s)); assert(s);
    s->data = malloc(capacity); assert(s->data); s->capacity = capacity;
    assert(pthread_mutex_init(&s->lock, NULL) == 0);
    assert(pthread_cond_init(&s->space, NULL) == 0);
    return s;
}
void vStreamBufferDelete(StreamBufferHandle_t s)
{
    pthread_cond_destroy(&s->space); pthread_mutex_destroy(&s->lock);
    free(s->data); free(s);
}
size_t xStreamBufferSend(StreamBufferHandle_t s, const void *data, size_t count, unsigned ticks)
{
    pthread_mutex_lock(&s->lock);
    if (s->count == s->capacity && ticks) {
        struct timespec limit;
        clock_gettime(CLOCK_REALTIME, &limit);
        limit.tv_nsec += (long)ticks * 100000;
        limit.tv_sec += limit.tv_nsec / 1000000000; limit.tv_nsec %= 1000000000;
        pthread_cond_timedwait(&s->space, &s->lock, &limit);
    }
    size_t n = count < s->capacity - s->count ? count : s->capacity - s->count;
    for (size_t i = 0; i < n; ++i) s->data[(s->read + s->count + i) % s->capacity] = ((const unsigned char *)data)[i];
    s->count += n;
    pthread_mutex_unlock(&s->lock);
    return n;
}
size_t xStreamBufferReceive(StreamBufferHandle_t s, void *data, size_t count, unsigned ticks)
{
    (void)ticks;
    pthread_mutex_lock(&s->lock);
    size_t n = count < s->count ? count : s->count;
    for (size_t i = 0; i < n; ++i) ((unsigned char *)data)[i] = s->data[(s->read + i) % s->capacity];
    s->read = (s->read + n) % s->capacity; s->count -= n;
    pthread_cond_signal(&s->space); pthread_mutex_unlock(&s->lock);
    return n;
}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s)
{
    pthread_mutex_lock(&s->lock); size_t count = s->count; pthread_mutex_unlock(&s->lock);
    return count;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    struct test_http *http = calloc(1, sizeof(*http)); assert(http);
    http->config = *config; atomic_fetch_add(&s_clients, 1); return http;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t http, const char *key, const char *value)
{
    if (!strcmp(key, "Range")) {
        assert(!strncmp(value, "bytes=", 6));
        http->position = http->start = (size_t)strtoull(value + 6, NULL, 10); http->range = true;
    }
    if (!strcmp(key, "If-Range")) assert(!strcmp(value, "\"stable\""));
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t http, int length)
{
    (void)http; (void)length; atomic_fetch_add(&s_opens, 1); return ESP_OK;
}
static void header(esp_http_client_handle_t http, char *key, char *value)
{
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER,
        .user_data = http->config.user_data, .header_key = key, .header_value = value};
    assert(http->config.event_handler(&event) == ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http)
{
    header(http, "ETag", s_scenario == CHANGED_ETAG && http->range ? "\"changed\"" : "\"stable\"");
    if (http->range && s_scenario != IGNORE_RANGE) {
        char range[96];
        snprintf(range, sizeof(range), "bytes %zu-%zu/%zu", http->start + (s_scenario == BAD_RANGE), s_length - 1, s_length);
        header(http, "Content-Range", range);
    }
    return s_scenario == CHUNKED ? 0 : (int64_t)(s_length - http->start);
}
int esp_http_client_get_status_code(esp_http_client_handle_t http) { return http->range && s_scenario != IGNORE_RANGE ? 206 : 200; }
bool esp_http_client_is_chunked_response(esp_http_client_handle_t http) { (void)http; return s_scenario == CHUNKED; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t http) { (void)http; return ESP_FAIL; }
esp_err_t esp_http_client_get_url(esp_http_client_handle_t http, char *out, int length)
{
    (void)http; (void)out; (void)length; return ESP_FAIL;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t http, int ms) { (void)http; (void)ms; return ESP_OK; }
int esp_http_client_read(esp_http_client_handle_t http, char *out, int length)
{
    bool fault = s_scenario == DISCONNECT || s_scenario == EARLY_EOF || s_scenario == IGNORE_RANGE ||
                 s_scenario == BAD_RANGE || s_scenario == TRUNCATED || s_scenario == CHANGED_ETAG;
    if (http->position >= 12288 && fault && (!s_broken || s_scenario == TRUNCATED)) {
        s_broken = true; return s_scenario == EARLY_EOF ? 0 : -1;
    }
    if (s_scenario == TIMEOUT && s_timeouts < 2) { ++s_timeouts; return -ESP_ERR_HTTP_EAGAIN; }
    size_t n = (size_t)length < s_length - http->position ? (size_t)length : s_length - http->position;
    for (size_t i = 0; i < n; ++i) out[i] = (char)((http->position + i) % 251);
    http->position += n;
    return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http) { return http->position == s_length; }
esp_err_t esp_http_client_close(esp_http_client_handle_t http) { (void)http; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http)
{
    atomic_fetch_sub(&s_clients, 1); free(http); return ESP_OK;
}
static void wait_tasks(void)
{
    for (unsigned i = 0; i < 1000 && atomic_load(&s_tasks); ++i) sleep_us(1000);
    assert(atomic_load(&s_tasks) == 0); assert(atomic_load(&s_clients) == 0);
}
static void run(enum scenario scenario, bool success)
{
    s_scenario = scenario; s_broken = false; s_timeouts = 0; atomic_store(&s_opens, 0);
    audio_download_t *download = NULL;
    assert(audio_download_start("https://test.invalid/audio", &download) == ESP_OK);
    size_t received = 0;
    int64_t deadline = esp_timer_get_time() + 120000000;
    while (!audio_download_finished(download) || audio_download_available(download)) {
        assert(esp_timer_get_time() < deadline);
        assert(audio_download_available(download) <= AUDIO_STREAM_NETWORK_BYTES);
        unsigned char data[333];
        size_t n = audio_download_read(download, data, sizeof(data));
        for (size_t i = 0; i < n; ++i) assert(data[i] == (received + i) % 251);
        received += n;
        if (!n) sleep_us(100);
    }
    assert((audio_download_result(download) == ESP_OK) == success);
    if (success) assert(received == s_length); else assert(received == 12288);
    if (scenario == DISCONNECT || scenario == EARLY_EOF) assert(atomic_load(&s_opens) == 2);
    if (scenario == TRUNCATED) assert(atomic_load(&s_opens) == 7);
    audio_download_stop(download); wait_tasks();
}
int main(void)
{
    run(NORMAL, true); run(TIMEOUT, true); run(DISCONNECT, true); run(EARLY_EOF, true);
    run(IGNORE_RANGE, false); run(BAD_RANGE, false); run(TRUNCATED, false);
    run(CHUNKED, true); run(CHANGED_ETAG, false);
    s_scenario = CANCEL_FULL;
    audio_download_t *download = NULL;
    assert(audio_download_start("https://test.invalid/audio", &download) == ESP_OK);
    int64_t deadline = esp_timer_get_time() + 10000000;
    while (audio_download_available(download) < AUDIO_STREAM_NETWORK_BYTES) {
        assert(esp_timer_get_time() < deadline); sleep_us(100);
    }
    audio_download_stop(download); wait_tasks();
    char long_url[1025];memset(long_url,'x',sizeof(long_url));memcpy(long_url,"https://test.invalid/",21);long_url[1024]=0;
    assert(audio_download_start(long_url,&download)==ESP_ERR_INVALID_ARG);
    long_url[1023]=0;
    assert(audio_download_start(long_url,&download)==ESP_OK);
    audio_download_stop(download);wait_tasks();
    puts("Audio downloader: PASS (10 concurrent fault scenarios)");
    return 0;
}
