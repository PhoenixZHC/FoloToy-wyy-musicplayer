#include "audio_stream.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "bsp_audio.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mp3dec.h"

#include <stdlib.h>

#define AUDIO_STREAM_URL_LEN 512
/* Keep enough compressed audio for a frame while leaving TLS headroom for
 * playlist/metadata requests made while playback remains active. */
#define AUDIO_STREAM_INPUT_BYTES (8 * 1024)
#define AUDIO_STREAM_PCM_SAMPLES 2304
#define AUDIO_STREAM_TASK_STACK 8192
#define AUDIO_STREAM_DEFAULT_VOLUME 35
#define AUDIO_STREAM_MIN_VOLUME 0
#define AUDIO_STREAM_MAX_VOLUME 100
#define AUDIO_STREAM_OUTPUT_CHANNELS 1
#define AUDIO_STREAM_TASK_PRIORITY 6

static const char *TAG = "audio_stream";

static TaskHandle_t s_task;
static char s_url[AUDIO_STREAM_URL_LEN];
static volatile bool s_stop_requested;
static volatile bool s_pause_requested;
static volatile audio_stream_state_t s_state = AUDIO_STREAM_STOPPED;
static volatile uint32_t s_elapsed_ms;
static volatile int s_volume_percent = AUDIO_STREAM_DEFAULT_VOLUME;
static volatile bool s_codec_ready;

static int clamp_volume(int percent)
{
    if (percent < AUDIO_STREAM_MIN_VOLUME) return AUDIO_STREAM_MIN_VOLUME;
    if (percent > AUDIO_STREAM_MAX_VOLUME) return AUDIO_STREAM_MAX_VOLUME;
    return percent;
}

static int skip_id3v2_tag(const uint8_t *data, int bytes)
{
    if (bytes < 10 || memcmp(data, "ID3", 3) != 0) return 0;
    return 10 + ((data[6] & 0x7f) << 21) + ((data[7] & 0x7f) << 14) +
           ((data[8] & 0x7f) << 7) + (data[9] & 0x7f);
}

static void compact_buffer(uint8_t *buffer, int *bytes_left, uint8_t **read_ptr)
{
    if (*bytes_left > 0 && *read_ptr != buffer) {
        memmove(buffer, *read_ptr, (size_t)*bytes_left);
    }
    *read_ptr = buffer;
}

static bool wait_if_paused(void)
{
    while (!s_stop_requested && s_pause_requested) {
        s_state = AUDIO_STREAM_PAUSED;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_stop_requested) return false;
    s_state = AUDIO_STREAM_PLAYING;
    return true;
}

static esp_err_t decode_and_play(esp_http_client_handle_t http,
                                 uint8_t *input,
                                 int16_t *pcm,
                                 HMP3Decoder decoder)
{
    uint8_t *read_ptr = input;
    int bytes_left = 0;
    int skipped_id3 = 0;
    int id3_remaining = 0;
    uint32_t current_rate = 0;
    int decode_errors = 0;
    bool eof = false;
    bool decoder_needs_data = false;
    int64_t report_us = esp_timer_get_time();
    int64_t max_read_us = 0, max_decode_us = 0, max_write_us = 0;
    unsigned frames = 0, network_reads = 0, network_bytes = 0;

    for (;;) {
        if (!wait_if_paused()) return ESP_OK;

        if (!eof && audio_stream_needs_refill(bytes_left, decoder_needs_data)) {
            compact_buffer(input, &bytes_left, &read_ptr);
            int space = AUDIO_STREAM_INPUT_BYTES - bytes_left;
            if (space <= 0) return ESP_ERR_INVALID_RESPONSE;
            int64_t start = esp_timer_get_time();
            int read = esp_http_client_read(http, (char *)input + bytes_left, space);
            int64_t cost = esp_timer_get_time() - start;
            if (cost > max_read_us) max_read_us = cost;
            network_reads++;
            if (read > 0) network_bytes += (unsigned)read;
            if (read < 0) {
                ESP_LOGE(TAG, "http read failed");
                return ESP_FAIL;
            }
            if (read == 0 && bytes_left == 0) return ESP_OK;
            if (read == 0) eof = true;
            bytes_left += read;
            decoder_needs_data = false;
        }

        if (id3_remaining > 0) {
            int discard = bytes_left < id3_remaining ? bytes_left : id3_remaining;
            read_ptr += discard;
            bytes_left -= discard;
            id3_remaining -= discard;
            if (id3_remaining > 0) continue;
            skipped_id3 = 1;
        }

        if (!skipped_id3 && bytes_left >= 10) {
            int id3_len = skip_id3v2_tag(read_ptr, bytes_left);
            if (id3_len > 0) {
                int discard = bytes_left < id3_len ? bytes_left : id3_len;
                read_ptr += discard;
                bytes_left -= discard;
                id3_remaining = id3_len - discard;
                if (id3_remaining > 0) continue;
            }
            skipped_id3 = 1;
        }

        int sync = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync < 0) {
            if (eof) return ESP_OK;
            if (bytes_left > 1) {
                read_ptr += bytes_left - 1;
                bytes_left = 1;
            }
            decoder_needs_data = true;
            continue;
        }
        read_ptr += sync;
        bytes_left -= sync;

        int64_t decode_start = esp_timer_get_time();
        int err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm, 0);
        int64_t decode_us = esp_timer_get_time() - decode_start;
        if (decode_us > max_decode_us) max_decode_us = decode_us;
        if (err != 0) {
            decoder_needs_data = true;
            if (eof) return ESP_ERR_INVALID_RESPONSE;
            decode_errors++;
            if (decode_errors <= 3 || decode_errors % 50 == 0) {
                ESP_LOGW(TAG, "mp3 decode retry: err=%d buffered=%d", err, bytes_left);
            }
            if (bytes_left == AUDIO_STREAM_INPUT_BYTES) {
                ESP_LOGE(TAG, "mp3 decode failed with full buffer: err=%d", err);
                return ESP_FAIL;
            }
            continue;
        }
        decode_errors = 0;

        MP3FrameInfo frame = {0};
        MP3GetLastFrameInfo(decoder, &frame);
        if (frame.samprate <= 0 || frame.bitsPerSample != 16 || frame.outputSamps <= 0) {
            ESP_LOGE(TAG, "unsupported mp3 frame: rate=%d bits=%d output_samps=%d",
                     frame.samprate,
                     frame.bitsPerSample,
                     frame.outputSamps);
            return ESP_FAIL;
        }

        if ((uint32_t)frame.samprate != current_rate) {
            current_rate = (uint32_t)frame.samprate;
            esp_err_t format_err = bsp_audio_set_format(current_rate, 16, AUDIO_STREAM_OUTPUT_CHANNELS);
            if (format_err != ESP_OK) return format_err;
            s_codec_ready = true;
            bsp_audio_set_volume((uint8_t)s_volume_percent);
            ESP_LOGI(TAG, "mp3 frame: source_rate=%ld channels=%d output_samps=%d bitrate=%d",
                     (long)frame.samprate,
                     frame.nChans,
                     frame.outputSamps,
                     frame.bitrate);
        }

        int mono_samples = 0;
        if (frame.nChans == 2) {
            mono_samples = frame.outputSamps / 2;
            for (int i = 0; i < mono_samples; i++) {
                int left = pcm[i * 2];
                int right = pcm[i * 2 + 1];
                pcm[i] = (int16_t)((left + right) / 2);
            }
        } else if (frame.nChans == 1) {
            mono_samples = frame.outputSamps;
        } else {
            ESP_LOGE(TAG, "unsupported mp3 channels: %d", frame.nChans);
            return ESP_ERR_NOT_SUPPORTED;
        }

        int64_t write_start = esp_timer_get_time();
        if (bsp_audio_write(pcm, (size_t)mono_samples * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGE(TAG, "audio write failed: samples=%d", mono_samples);
            return ESP_FAIL;
        }
        s_elapsed_ms += (uint32_t)(((uint64_t)mono_samples * 1000U) / current_rate);
        int64_t now = esp_timer_get_time();
        int64_t write_us = now - write_start;
        if (write_us > max_write_us) max_write_us = write_us;
        frames++;
        if (now - report_us >= 5000000) {
            ESP_LOGI(TAG, "timing frames=%u reads=%u bytes=%u buffered=%d max_read_us=%lld max_decode_us=%lld max_write_us=%lld free=%u",
                     frames, network_reads, network_bytes, bytes_left, (long long)max_read_us,
                     (long long)max_decode_us, (long long)max_write_us,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            report_us=now;frames=network_reads=network_bytes=0;max_read_us=max_decode_us=max_write_us=0;
        }
    }
}

static void audio_stream_task(void *arg)
{
    (void)arg;
    esp_err_t result = ESP_OK;
    uint8_t *input = NULL;
    int16_t *pcm = NULL;
    HMP3Decoder decoder = NULL;
    esp_http_client_handle_t http = NULL;

    input = heap_caps_malloc(AUDIO_STREAM_INPUT_BYTES, MALLOC_CAP_8BIT);
    pcm = heap_caps_malloc(AUDIO_STREAM_PCM_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT);
    decoder = MP3InitDecoder();
    if (input == NULL || pcm == NULL || decoder == NULL) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    esp_http_client_config_t config = {
        .url = s_url,
        .timeout_ms = 10000,
        /* Compressed input is retained separately; HTTP RX need not duplicate it. */
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    http = esp_http_client_init(&config);
    if (http == NULL) {
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    ESP_LOGI(TAG, "opening audio stream: url_len=%u", (unsigned)strlen(s_url));
    int64_t content_length = -1;
    int status = 0;
    for (unsigned redirects = 0; redirects <= 3; ++redirects) {
        result = esp_http_client_open(http, 0);
        if (result != ESP_OK) goto done;
        content_length = esp_http_client_fetch_headers(http);
        status = esp_http_client_get_status_code(http);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) break;
        if (redirects == 3) { result = ESP_ERR_INVALID_RESPONSE; goto done; }
        result = esp_http_client_set_redirection(http);
        if (result != ESP_OK) goto done;
        char redirected[AUDIO_STREAM_URL_LEN];
        result = esp_http_client_get_url(http, redirected, sizeof(redirected));
        if (result != ESP_OK) goto done;
        if (strncmp(s_url, "https://", 8) == 0 && strncmp(redirected, "https://", 8) != 0) {
            result = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        esp_http_client_close(http);
    }
    ESP_LOGI(TAG, "audio http headers: status=%d content_length=%lld",
             status,
             (long long)content_length);
    if (!audio_stream_accepts_http_headers(status, content_length)) {
        result = ESP_FAIL;
        goto done;
    }

    ESP_LOGI(TAG, "before audio init: free=%u largest=%u minimum=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    result = bsp_audio_init();
    if (result != ESP_OK) goto done;
    ESP_LOGI(TAG, "after audio init: free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    s_state = AUDIO_STREAM_PLAYING;
    result = decode_and_play(http, input, pcm, decoder);

done:
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "audio stream failed: %s", esp_err_to_name(result));
        s_state = AUDIO_STREAM_ERROR;
    } else {
        s_state = AUDIO_STREAM_STOPPED;
    }
    if (http) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    if (decoder) MP3FreeDecoder(decoder);
    free(input);
    free(pcm);
    s_codec_ready = false;
    bsp_audio_deinit();
    s_task = NULL;
    s_stop_requested = false;
    s_pause_requested = false;
    vTaskDelete(NULL);
}

esp_err_t audio_stream_start(const char *url)
{
    if (url == NULL || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (strlen(url) >= sizeof(s_url)) return ESP_ERR_INVALID_ARG;

    audio_stream_stop();
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;
    strcpy(s_url, url);
    s_stop_requested = false;
    s_pause_requested = false;
    s_elapsed_ms = 0;
    s_state = AUDIO_STREAM_PLAYING;
    BaseType_t ok = xTaskCreate(audio_stream_task, "audio_stream", AUDIO_STREAM_TASK_STACK,
                                NULL, AUDIO_STREAM_TASK_PRIORITY, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        s_state = AUDIO_STREAM_ERROR;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t audio_stream_pause(void)
{
    if (s_state != AUDIO_STREAM_PLAYING || s_task == NULL) return ESP_ERR_INVALID_STATE;
    s_pause_requested = true;
    s_state = AUDIO_STREAM_PAUSED;
    return ESP_OK;
}

esp_err_t audio_stream_resume(void)
{
    if (s_state != AUDIO_STREAM_PAUSED || s_task == NULL) return ESP_ERR_INVALID_STATE;
    s_pause_requested = false;
    s_state = AUDIO_STREAM_PLAYING;
    return ESP_OK;
}

void audio_stream_stop(void)
{
    if (s_task == NULL) {
        s_state = AUDIO_STREAM_STOPPED;
        return;
    }
    audio_stream_request_stop();
    for (int i = 0; i < 100 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_task != NULL) {
        ESP_LOGW(TAG, "audio stream stop timed out; task will clean up asynchronously");
        return;
    }
    s_state = AUDIO_STREAM_STOPPED;
    s_stop_requested = false;
    s_pause_requested = false;
}
#else
static audio_stream_state_t s_state = AUDIO_STREAM_STOPPED;
static uint32_t s_elapsed_ms;
static int s_volume_percent = 35;

static int clamp_volume(int percent)
{
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

esp_err_t audio_stream_start(const char *url)
{
    if (url == NULL || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    s_elapsed_ms = 0;
    s_state = AUDIO_STREAM_ERROR;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t audio_stream_pause(void)
{
    return ESP_ERR_INVALID_STATE;
}

esp_err_t audio_stream_resume(void)
{
    return ESP_ERR_INVALID_STATE;
}

void audio_stream_stop(void)
{
    s_state = AUDIO_STREAM_STOPPED;
    s_elapsed_ms = 0;
}
#endif

bool audio_stream_accepts_http_headers(int status_code, int64_t content_length)
{
    (void)content_length;
    return status_code >= 200 && status_code < 300;
}

bool audio_stream_needs_refill(int buffered_bytes, bool decoder_needs_data)
{
    return buffered_bytes < 2048 || decoder_needs_data;
}

void audio_stream_request_stop(void)
{
#ifdef ESP_PLATFORM
    s_stop_requested = true;
#endif
    s_state = AUDIO_STREAM_STOPPED;
}

audio_stream_state_t audio_stream_state(void)
{
    return s_state;
}

bool audio_stream_is_active(void)
{
    return s_state == AUDIO_STREAM_PLAYING || s_state == AUDIO_STREAM_PAUSED;
}

uint32_t audio_stream_elapsed_ms(void)
{
    return s_elapsed_ms;
}

int audio_stream_volume(void)
{
    return s_volume_percent;
}

esp_err_t audio_stream_set_volume(int percent)
{
    s_volume_percent = clamp_volume(percent);
#ifdef ESP_PLATFORM
    if (s_codec_ready) {
        bsp_audio_set_volume((uint8_t)s_volume_percent);
    }
#endif
    return ESP_OK;
}

int audio_stream_adjust_volume(int delta)
{
    (void)audio_stream_set_volume(s_volume_percent + delta);
    return s_volume_percent;
}
