#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_STREAM_NETWORK_BYTES (16U * 1024U)
#define AUDIO_STREAM_PREBUFFER_BYTES (8U * 1024U)

bool audio_stream_buffer_ready(size_t buffered, bool finished);
bool audio_stream_body_complete(int64_t expected, uint64_t received, bool transport_complete);
bool audio_stream_validate_range(const char *header, uint64_t offset, int64_t previous_total,
                                 int64_t response_length, int64_t *total);
unsigned audio_stream_retry_delay_ms(unsigned attempt);
