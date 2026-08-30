#include <assert.h>
#include <stddef.h>

#include "audio_stream.h"

int main(void)
{
    assert(audio_stream_accepts_http_headers(200, -1));
    assert(audio_stream_accepts_http_headers(206, -1));
    assert(!audio_stream_accepts_http_headers(404, 128));
    assert(!audio_stream_accepts_http_headers(500, 128));
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
