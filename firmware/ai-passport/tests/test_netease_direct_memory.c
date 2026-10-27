#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Track the real encoder's allocations. Shared mbedTLS internals are outside
 * this budget; this is not a complete TLS or device heap model. */
typedef union { max_align_t alignment; size_t bytes; } allocation_t;
static size_t used, peak, budget = SIZE_MAX;

static void *budget_malloc(size_t bytes)
{
    if (bytes > budget - used || bytes > SIZE_MAX - sizeof(allocation_t)) return NULL;
    allocation_t *p = malloc(sizeof(*p) + bytes);
    if (!p) return NULL;
    p->bytes = bytes;
    used += bytes;
    if (used > peak) peak = used;
    return p + 1;
}

static void budget_free(void *ptr)
{
    if (!ptr) return;
    allocation_t *p = (allocation_t *)ptr - 1;
    assert(used >= p->bytes);
    used -= p->bytes;
    free(p);
}

#define malloc budget_malloc
#define free budget_free
#include "../../components/netease_direct_client/netease_direct_client.c"
#undef malloc
#undef free

int main(void)
{
    char json[768];
    size_t n = (size_t)snprintf(json, sizeof(json), "{\"c\":\"[");
    for (unsigned i = 0; i < 8; ++i)
        n += (size_t)snprintf(json + n, sizeof(json) - n,
            "%s{\\\"id\\\":%llu}", i ? "," : "", 9007199254740000ULL + i);
    snprintf(json + n, sizeof(json) - n,
        "]\",\"csrf_token\":\"0123456789abcdef0123456789abcdef\"}");
    const unsigned char key[] = "0123456789abcdef";
    char expected[4096], actual[4096];
    assert(netease_direct_build_weapi_body_with_key(json, key, expected, sizeof(expected)) == ESP_OK);
    assert(used == 0);
    printf("WEAPI encoder unrestricted peak=%zu bytes\n", peak);
    fflush(stdout);
    budget = 3072;
    peak = 0;
    assert(netease_direct_build_weapi_body_with_key(json, key, actual, sizeof(actual)) == ESP_OK);
    assert(strcmp(actual, expected) == 0 && used == 0 && peak <= budget);
    size_t required = strlen(expected) + 1;
    assert(netease_direct_build_weapi_body_with_key(json, key, actual, required) == ESP_OK);
    assert(strcmp(actual, expected) == 0 && used == 0);
    assert(netease_direct_build_weapi_body_with_key(json, key, actual, required - 1) == ESP_ERR_INVALID_SIZE);
    assert(used == 0);
    memset(actual, 0x5a, sizeof(actual));
    assert(netease_direct_build_weapi_body_with_key(json, key, actual, 64) == ESP_ERR_INVALID_SIZE);
    assert(used == 0 && actual[64] == 0x5a);
    budget = 1;
    assert(netease_direct_build_weapi_body_with_key(json, key, actual, sizeof(actual)) == ESP_ERR_NO_MEM);
    assert(used == 0);
    puts("netease_direct_memory: PASS");
    return 0;
}
