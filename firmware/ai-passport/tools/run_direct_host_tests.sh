#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH_VALUE="${IDF_PATH:?Activate ESP-IDF or set IDF_PATH}"
CJSON_DIR="$IDF_PATH_VALUE/components/json/cJSON"
OUT="${DIRECT_TEST_OUT:-$(mktemp -d)/test_direct}"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DNETEASE_DIRECT_HOST_TEST ${DIRECT_TEST_CFLAGS:-} \
  -I"$ROOT/../components/netease_direct_client/include" -I"$ROOT/../components/netease_direct_client" \
  -I"$CJSON_DIR" \
  "$ROOT/tests/test_netease_direct_client.c" \
  "$ROOT/../components/netease_direct_client/netease_direct_client.c" \
  "$ROOT/../components/netease_direct_client/direct_stream.c" \
  "$CJSON_DIR/cJSON.c" \
  ${DIRECT_TEST_LDFLAGS:-} -lmbedcrypto -o "$OUT"
"$OUT"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DNETEASE_DIRECT_HOST_TEST ${DIRECT_TEST_CFLAGS:-} \
  -I"$ROOT/tests/direct_stubs" \
  -I"$ROOT/../components/netease_direct_client/include" -I"$ROOT/../components/netease_direct_client" \
  -I"$CJSON_DIR" \
  "$ROOT/tests/test_netease_direct_http.c" \
  "$ROOT/../components/netease_direct_client/netease_direct_client.c" \
  "$ROOT/../components/netease_direct_client/direct_stream.c" \
  "$CJSON_DIR/cJSON.c" \
  ${DIRECT_TEST_LDFLAGS:-} -lmbedcrypto -o "${OUT}_http"
"${OUT}_http"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DNETEASE_DIRECT_HOST_TEST ${DIRECT_TEST_CFLAGS:-} \
  -I"$ROOT/../components/netease_direct_client/include" -I"$ROOT/../components/netease_direct_client" \
  -I"$CJSON_DIR" "$ROOT/tests/test_netease_direct_memory.c" "$CJSON_DIR/cJSON.c" \
  ${DIRECT_TEST_LDFLAGS:-} -lmbedcrypto -o "${OUT}_memory"
"${OUT}_memory"
