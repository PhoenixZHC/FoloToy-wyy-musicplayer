#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct audio_download audio_download_t;

/* One producer and one decoder own this bounded compressed-audio queue. */
esp_err_t audio_download_start(const char *url, audio_download_t **download);
size_t audio_download_read(audio_download_t *download, void *buffer, size_t length);
size_t audio_download_available(audio_download_t *download);
bool audio_download_finished(audio_download_t *download);
esp_err_t audio_download_result(audio_download_t *download);
/* Cancel, join the producer, then free its queue; never delete a live task. */
void audio_download_stop(audio_download_t *download);
