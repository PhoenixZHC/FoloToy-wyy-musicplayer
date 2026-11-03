#include <assert.h>
#include <stddef.h>

#include "audio_stream.h"
#include "audio_stream_policy.h"

int main(void)
{
    assert(audio_stream_needs_refill(0, false));
    assert(audio_stream_needs_refill(2047, false));
    assert(!audio_stream_needs_refill(2048, false));
    assert(!audio_stream_needs_refill(7774, false));
    assert(audio_stream_needs_refill(4096, true));
    assert(audio_stream_accepts_http_headers(200, -1));
    assert(audio_stream_accepts_http_headers(206, -1));
    assert(!audio_stream_accepts_http_headers(404, 128));
    assert(!audio_stream_accepts_http_headers(500, 128));
    assert(!audio_stream_accepts_http_headers(204, 0));
    assert(!audio_stream_buffer_ready(8191, false));
    assert(audio_stream_buffer_ready(8192, false));
    assert(audio_stream_buffer_ready(100, true));
    assert(!audio_stream_body_complete(1000, 999, true));
    assert(!audio_stream_body_complete(1000, 1000, false));
    assert(audio_stream_body_complete(1000, 1000, true));
    assert(audio_stream_body_complete(-1, 1000, true));
    assert(!audio_stream_body_complete(-1, 1000, false));
    int64_t total = -1;
    assert(audio_stream_validate_range("bytes 100-999/1000", 100, 1000, 900, &total));
    assert(total == 1000);
    assert(!audio_stream_validate_range("bytes 0-999/1000", 100, 1000, 1000, &total));
    assert(!audio_stream_validate_range("bytes 100-999/1001", 100, 1000, 900, &total));
    assert(!audio_stream_validate_range("bytes 100-999/1000", 100, 1000, 899, &total));
    assert(!audio_stream_validate_range("bytes 100-1000/1000", 100, 1000, 901, &total));
    assert(!audio_stream_validate_range("bytes 100-999/1000junk", 100, 1000, 900, &total));
    assert(!audio_stream_validate_range("bytes 184467440737095516160-999/1000", 100, 1000, 900, &total));
    assert(!audio_stream_validate_range(NULL, 100, 1000, 900, &total));
    assert(audio_stream_retry_delay_ms(0) == 250);
    assert(audio_stream_retry_delay_ms(4) == 4000);
    assert(audio_stream_retry_delay_ms(100) == 4000);
    assert(audio_stream_elapsed_ms() == 0);
    assert(audio_stream_volume() == 35);
    assert(audio_stream_set_volume(120) == ESP_OK);
    assert(audio_stream_volume() == 100);
    assert(audio_stream_adjust_volume(-150) == 0);
    assert(audio_stream_volume() == 0);
    assert(audio_stream_adjust_volume(12) == 12);
    assert(audio_stream_volume() == 12);
    assert(audio_stream_is_active() == false);

    assert(audio_stream_start(NULL) == ESP_ERR_INVALID_ARG);
    assert(audio_stream_start("") == ESP_ERR_INVALID_ARG);
    assert(audio_stream_pause() == ESP_ERR_INVALID_STATE);
    assert(audio_stream_resume() == ESP_ERR_INVALID_STATE);
    audio_stream_request_stop();
    audio_stream_stop();
    return 0;
}
