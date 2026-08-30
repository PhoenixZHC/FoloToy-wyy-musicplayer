#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 0x101
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_STREAM_STOPPED = 0,
    AUDIO_STREAM_PLAYING,
    AUDIO_STREAM_PAUSED,
    AUDIO_STREAM_ERROR,
} audio_stream_state_t;

esp_err_t audio_stream_start(const char *url);
esp_err_t audio_stream_pause(void);
esp_err_t audio_stream_resume(void);
void audio_stream_request_stop(void);
void audio_stream_stop(void);
audio_stream_state_t audio_stream_state(void);
bool audio_stream_is_active(void);
bool audio_stream_accepts_http_headers(int status_code, int64_t content_length);
uint32_t audio_stream_elapsed_ms(void);
int audio_stream_volume(void);
esp_err_t audio_stream_set_volume(int percent);
int audio_stream_adjust_volume(int delta);

#ifdef __cplusplus
}
#endif
