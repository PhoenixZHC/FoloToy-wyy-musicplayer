#include "audio_stream_policy.h"

#include <limits.h>
#include <string.h>

bool audio_stream_buffer_ready(size_t buffered, bool finished)
{
    return buffered >= AUDIO_STREAM_PREBUFFER_BYTES || finished;
}

bool audio_stream_body_complete(int64_t expected, uint64_t received, bool transport_complete)
{
    return transport_complete && (expected < 0 || received == (uint64_t)expected);
}

/* Parse strictly without accepting integer overflow or trailing header data. */
static bool parse_u64(const char **text, uint64_t *value)
{
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    uint64_t result = 0;
    do {
        unsigned digit = (unsigned)(*p - '0');
        if (result > (UINT64_MAX - digit) / 10) return false;
        result = result * 10 + digit;
        ++p;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = result;
    return true;
}

bool audio_stream_validate_range(const char *header, uint64_t offset, int64_t previous_total,
                                 int64_t response_length, int64_t *total)
{
    if (!header || !total || strncmp(header, "bytes ", 6) != 0) return false;
    const char *p = header + 6;
    uint64_t first, last, length;
    if (!parse_u64(&p, &first) || *p++ != '-' || !parse_u64(&p, &last) ||
        *p++ != '/' || !parse_u64(&p, &length) || *p != 0) return false;
    if (first != offset || last < first || last >= length || length > INT64_MAX ||
        response_length < 0 || (uint64_t)response_length != last - first + 1 ||
        (previous_total >= 0 && (uint64_t)previous_total != length)) return false;
    *total = (int64_t)length;
    return true;
}

unsigned audio_stream_retry_delay_ms(unsigned attempt)
{
    return 250U << (attempt < 4 ? attempt : 4);
}
