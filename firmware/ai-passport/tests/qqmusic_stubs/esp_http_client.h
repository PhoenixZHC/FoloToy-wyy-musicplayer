#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct fake_http *esp_http_client_handle_t;
enum { HTTP_METHOD_POST,HTTP_METHOD_GET };
typedef struct { esp_http_client_handle_t client;int event_id; } esp_http_client_event_t;
typedef struct {
    const char *url;
    int method,timeout_ms,buffer_size,buffer_size_tx;
    esp_err_t (*crt_bundle_attach)(void *);
    bool disable_auto_redirect;
    bool is_async;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle,const char *name,const char *value);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t handle,int timeout_ms);
esp_err_t esp_http_client_open(esp_http_client_handle_t handle,int length);
int esp_http_client_write(esp_http_client_handle_t handle,const char *data,int length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle);
int esp_http_client_get_status_code(esp_http_client_handle_t handle);
int esp_http_client_read(esp_http_client_handle_t handle,char *data,int length);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t handle);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle);
