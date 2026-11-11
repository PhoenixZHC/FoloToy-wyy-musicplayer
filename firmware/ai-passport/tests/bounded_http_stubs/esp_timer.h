#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct test_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void *);const char *name; } esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *config,esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t delay_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
int64_t esp_timer_get_time(void);
