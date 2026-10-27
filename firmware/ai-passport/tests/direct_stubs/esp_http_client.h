#pragma once
#include <stdbool.h>
#include "esp_err.h"
#define ESP_ERR_HTTP_FETCH_HEADER 0x7004
#define ESP_ERR_HTTP_WRITE_DATA 0x7003
typedef struct fake_http *esp_http_client_handle_t;
enum { HTTP_EVENT_ON_HEADER, HTTP_EVENT_ON_DATA, HTTP_METHOD_POST };
enum { HTTP_TLS_DYN_BUF_RX_STATIC = 1 };
typedef struct { int event_id; void *user_data; const char *header_key; const char *header_value; const char *data; int data_len; } esp_http_client_event_t;
typedef struct {
    const char *url; int method, timeout_ms, buffer_size, buffer_size_tx;
    esp_err_t (*crt_bundle_attach)(void *); bool disable_auto_redirect;
    esp_err_t (*event_handler)(esp_http_client_event_t *); void *user_data;
    int tls_dyn_buf_strategy;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t, const char *, int);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t, void *);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t, const char *);
