#pragma once
#include "esp_err.h"
#include <stddef.h>
typedef unsigned nvs_handle_t;
enum { NVS_READONLY, NVS_READWRITE };
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle);
esp_err_t nvs_get_str(nvs_handle_t handle,const char *key,char *value,size_t *length);
esp_err_t nvs_set_str(nvs_handle_t handle,const char *key,const char *value);
esp_err_t nvs_erase_key(nvs_handle_t handle,const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
