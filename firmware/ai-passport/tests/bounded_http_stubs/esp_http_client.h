#pragma once
#include "esp_err.h"
typedef struct test_http *esp_http_client_handle_t;
#define ESP_ERR_HTTP_CONNECTING 0x7001
#define ESP_ERR_HTTP_EAGAIN 0x7002
enum { HTTP_EVENT_ON_CONNECTED,HTTP_EVENT_HEADERS_SENT };
typedef struct { esp_http_client_handle_t client;int event_id; } esp_http_client_event_t;
int esp_http_client_get_socket(esp_http_client_handle_t http);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t http,int timeout_ms);
esp_err_t esp_http_client_open(esp_http_client_handle_t http,int write_len);
