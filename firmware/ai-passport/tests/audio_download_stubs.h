#pragma once
/* Host-only boundary: firmware never includes these platform fakes. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_FINISHED 0x10c
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define MALLOC_CAP_8BIT 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef struct test_stream *StreamBufferHandle_t;
typedef struct test_http *esp_http_client_handle_t;
typedef struct {
    int event_id;
    void *user_data;
    char *header_key;
    char *header_value;
} esp_http_client_event_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {
    const char *url;
    int timeout_ms, buffer_size, buffer_size_tx;
    void (*crt_bundle_attach)(void);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
void test_log(const char *, const char *, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
void esp_crt_bundle_attach(void);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
int64_t esp_timer_get_time(void);
void vTaskDelay(unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelete(void *);
StreamBufferHandle_t xStreamBufferCreate(size_t, size_t);
void vStreamBufferDelete(StreamBufferHandle_t);
size_t xStreamBufferSend(StreamBufferHandle_t, const void *, size_t, unsigned);
size_t xStreamBufferReceive(StreamBufferHandle_t, void *, size_t, unsigned);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_chunked_response(esp_http_client_handle_t);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t);
esp_err_t esp_http_client_get_url(esp_http_client_handle_t, char *, int);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
