#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
out="${AUDIO_DOWNLOAD_TEST_OUT:-$(mktemp -d)/test_audio_download}"
stubs="${out}.headers"
mkdir -p "$stubs/freertos"
for header in esp_err.h esp_crt_bundle.h esp_heap_caps.h esp_http_client.h esp_log.h esp_timer.h \
              freertos/FreeRTOS.h freertos/stream_buffer.h freertos/task.h; do
    printf '#pragma once\n' > "$stubs/$header"
done
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -D_POSIX_C_SOURCE=200809L \
    -include "$root/tests/audio_download_stubs.h" \
    -I"$stubs" -I"$root/../components/audio_stream" \
    "$root/tests/test_audio_download.c" \
    "$root/../components/audio_stream/audio_download.c" \
    "$root/../components/audio_stream/audio_stream_policy.c" -o "$out"
"$out"
