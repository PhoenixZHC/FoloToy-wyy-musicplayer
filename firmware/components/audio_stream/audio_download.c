#include "audio_download.h"
#include "audio_stream_policy.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#define DOWNLOAD_CHUNK_BYTES 1024
#define DOWNLOAD_RETRIES 6
#define DOWNLOAD_STALL_US 30000000LL

struct audio_download {
    StreamBufferHandle_t queue;
    atomic_bool cancel;
    atomic_bool done;
    esp_err_t result;
    char url[512];
    char range[96];
    char etag[128];
    char modified[64];
    char validator[128];
    bool validator_is_etag;
    bool invalid_headers;
    uint64_t received;
    int64_t total;
};

static const char *TAG = "audio_download";

static void copy_header(audio_download_t *d, char *dst, size_t capacity, const char *value)
{
    if (strlen(value) >= capacity) { d->invalid_headers = true; return; }
    memcpy(dst, value, strlen(value) + 1);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    audio_download_t *d = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    if (!strcasecmp(event->header_key, "Content-Range")) {
        copy_header(d, d->range, sizeof(d->range), event->header_value);
    } else if (!strcasecmp(event->header_key, "ETag")) {
        copy_header(d, d->etag, sizeof(d->etag), event->header_value);
    } else if (!strcasecmp(event->header_key, "Last-Modified")) {
        copy_header(d, d->modified, sizeof(d->modified), event->header_value);
    } else if (!strcasecmp(event->header_key, "Content-Encoding") &&
               strcasecmp(event->header_value, "identity")) {
        d->invalid_headers = true;
    }
    return ESP_OK;
}

/* Reopen at the number of bytes already queued, preserving decoder state.
 * A server that ignores Range must never replay the beginning of the song. */
static esp_err_t open_http(audio_download_t *d, esp_http_client_handle_t *out)
{
    esp_http_client_config_t config = {
        .url = d->url,
        .timeout_ms = 3000,
        .buffer_size = DOWNLOAD_CHUNK_BYTES,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .user_data = d,
    };
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_set_header(http, "Accept-Encoding", "identity");
    if (err != ESP_OK) goto failed;
    if (d->received) {
        char range[48];
        snprintf(range, sizeof(range), "bytes=%" PRIu64 "-", d->received);
        err = esp_http_client_set_header(http, "Range", range);
        if (err != ESP_OK) goto failed;
        if (d->validator[0]) {
            err = esp_http_client_set_header(http, "If-Range", d->validator);
            if (err != ESP_OK) goto failed;
        }
    }
    for (unsigned redirects = 0; redirects <= 3; ++redirects) {
        if (atomic_load(&d->cancel)) { err = ESP_ERR_INVALID_STATE; goto failed; }
        d->range[0] = d->etag[0] = d->modified[0] = 0;
        d->invalid_headers = false;
        err = esp_http_client_open(http, 0);
        if (err != ESP_OK) goto failed;
        int64_t length = esp_http_client_fetch_headers(http);
        if (length < 0) { err = ESP_FAIL; goto failed; }
        int status = esp_http_client_get_status_code(http);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (redirects == 3) { err = ESP_ERR_INVALID_RESPONSE; goto failed; }
            err = esp_http_client_set_redirection(http);
            if (err != ESP_OK) goto failed;
            char redirected[512];
            err = esp_http_client_get_url(http, redirected, sizeof(redirected));
            if (err != ESP_OK) goto failed;
            if (!strncmp(d->url, "https://", 8) && strncmp(redirected, "https://", 8)) {
                err = ESP_ERR_INVALID_RESPONSE;
                goto failed;
            }
            esp_http_client_close(http);
            continue;
        }
        ESP_LOGI(TAG, "HTTP status=%d offset=%" PRIu64 " length=%" PRId64,
                 status, d->received, length);
        if (status == 408 || status == 429 || status >= 500) { err = ESP_FAIL; goto failed; }
        if (d->invalid_headers) { err = ESP_ERR_INVALID_RESPONSE; goto failed; }
        if (d->received) {
            int64_t total;
            if (status != 206 || esp_http_client_is_chunked_response(http) ||
                !audio_stream_validate_range(d->range, d->received, d->total, length, &total)) {
                ESP_LOGE(TAG, "Resume rejected: response does not match requested bytes");
                err = ESP_ERR_INVALID_RESPONSE;
                goto failed;
            }
            const char *validator = d->validator_is_etag ? d->etag : d->modified;
            if (d->validator[0] && validator[0] && strcmp(d->validator, validator)) {
                err = ESP_ERR_INVALID_RESPONSE;
                goto failed;
            }
            d->total = total;
        } else {
            if (status != 200 || (!esp_http_client_is_chunked_response(http) && length <= 0)) {
                err = ESP_ERR_INVALID_RESPONSE;
                goto failed;
            }
            d->total = esp_http_client_is_chunked_response(http) ? -1 : length;
            if (d->etag[0] && strncmp(d->etag, "W/", 2)) {
                memcpy(d->validator, d->etag, strlen(d->etag) + 1);
                d->validator_is_etag = true;
            } else if (d->modified[0]) {
                memcpy(d->validator, d->modified, strlen(d->modified) + 1);
                d->validator_is_etag = false;
            }
        }
        err = esp_http_client_set_timeout_ms(http, 1000);
        if (err != ESP_OK) goto failed;
        *out = http;
        return ESP_OK;
    }
    err = ESP_ERR_INVALID_RESPONSE;
failed:
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return err;
}

static void download_task(void *arg)
{
    audio_download_t *d = arg;
    esp_http_client_handle_t http = NULL;
    esp_err_t result = ESP_FAIL;
    unsigned retries = 0, stable_bytes = 0;
    int64_t progress_us = esp_timer_get_time(), report_us = progress_us, max_read_us = 0;
    uint8_t chunk[DOWNLOAD_CHUNK_BYTES];
    while (!atomic_load(&d->cancel)) {
        if (!http) {
            result = open_http(d, &http);
            if (result != ESP_OK) goto retry;
        }
        int64_t started = esp_timer_get_time();
        int count = esp_http_client_read(http, (char *)chunk, sizeof(chunk));
        int64_t now = esp_timer_get_time();
        if (now - started > max_read_us) max_read_us = now - started;
        if (count > 0) {
            size_t sent = 0;
            while (sent < (size_t)count && !atomic_load(&d->cancel)) {
                size_t n = xStreamBufferSend(d->queue, chunk + sent, (size_t)count - sent, pdMS_TO_TICKS(50));
                sent += n;
                d->received += n;
            }
            progress_us = esp_timer_get_time();
            stable_bytes += (unsigned)sent;
            if (stable_bytes >= AUDIO_STREAM_PREBUFFER_BYTES) { retries = 0; stable_bytes = 0; }
            if (d->total >= 0 && d->received > (uint64_t)d->total) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            if (progress_us - report_us >= 5000000) {
                ESP_LOGI(TAG, "network received=%" PRIu64 " queued=%u max_read_us=%lld free=%u largest=%u",
                         d->received, (unsigned)xStreamBufferBytesAvailable(d->queue),
                         (long long)max_read_us, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
                report_us = progress_us;
                max_read_us = 0;
            }
            continue;
        }
        if (audio_stream_body_complete(d->total, d->received, esp_http_client_is_complete_data_received(http))) {
            result = ESP_OK;
            break;
        }
        if (count == -ESP_ERR_HTTP_EAGAIN && now - progress_us < 3000000) continue;
        ESP_LOGW(TAG, "Incomplete audio read=%d offset=%" PRIu64 " expected=%" PRId64,
                 count, d->received, d->total);
        result = ESP_FAIL;
retry:
        if (http) {
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            http = NULL;
        }
        if (atomic_load(&d->cancel)) break;
        if (result == ESP_ERR_NO_MEM || result == ESP_ERR_INVALID_RESPONSE ||
            retries >= DOWNLOAD_RETRIES || esp_timer_get_time() - progress_us >= DOWNLOAD_STALL_US) break;
        unsigned delay_ms = audio_stream_retry_delay_ms(retries++);
        ESP_LOGW(TAG, "Recover audio: attempt=%u delay_ms=%u offset=%" PRIu64,
                 retries, delay_ms, d->received);
        for (unsigned waited = 0; waited < delay_ms && !atomic_load(&d->cancel); waited += 25)
            vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (http) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    d->result = atomic_load(&d->cancel) ? ESP_OK : result;
    atomic_store(&d->done, true);
    vTaskDelete(NULL);
}

esp_err_t audio_download_start(const char *url, audio_download_t **out)
{
    if (!url || !out || strlen(url) >= 512) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    audio_download_t *d = calloc(1, sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;
    atomic_init(&d->cancel, false);
    atomic_init(&d->done, false);
    d->total = -1;
    memcpy(d->url, url, strlen(url) + 1);
    d->queue = xStreamBufferCreate(AUDIO_STREAM_NETWORK_BYTES, 1);
    if (!d->queue) { free(d); return ESP_ERR_NO_MEM; }
    if (xTaskCreate(download_task, "audio_download", 6144, d, 5, NULL) != pdPASS) {
        vStreamBufferDelete(d->queue);
        free(d);
        return ESP_ERR_NO_MEM;
    }
    *out = d;
    return ESP_OK;
}

size_t audio_download_read(audio_download_t *d, void *buffer, size_t length)
{
    return xStreamBufferReceive(d->queue, buffer, length, 0);
}

size_t audio_download_available(audio_download_t *d)
{
    return xStreamBufferBytesAvailable(d->queue);
}

bool audio_download_finished(audio_download_t *d)
{
    return atomic_load(&d->done);
}

esp_err_t audio_download_result(audio_download_t *d)
{
    return atomic_load(&d->done) ? d->result : ESP_ERR_NOT_FINISHED;
}

void audio_download_stop(audio_download_t *d)
{
    if (!d) return;
    atomic_store(&d->cancel, true);
    while (!atomic_load(&d->done)) vTaskDelay(pdMS_TO_TICKS(10));
    vStreamBufferDelete(d->queue);
    free(d);
}
