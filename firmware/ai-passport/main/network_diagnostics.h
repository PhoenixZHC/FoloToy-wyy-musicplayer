#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_DIAGNOSTICS_NOT_RUN,
    NETWORK_DIAGNOSTICS_RUNNING,
    NETWORK_DIAGNOSTICS_PASS,
    NETWORK_DIAGNOSTICS_FAIL,
} network_diagnostics_state_t;

typedef enum {
    NETWORK_DIAGNOSTICS_OK,
    NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED,
    NETWORK_DIAGNOSTICS_NO_IP,
    NETWORK_DIAGNOSTICS_TIME_INVALID,
    NETWORK_DIAGNOSTICS_DNS_FAILED,
    NETWORK_DIAGNOSTICS_DNS_TIMEOUT,
    NETWORK_DIAGNOSTICS_TLS_FAILED,
    NETWORK_DIAGNOSTICS_CERTIFICATE_FAILED,
    NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT,
    NETWORK_DIAGNOSTICS_WRITE_FAILED,
    NETWORK_DIAGNOSTICS_WRITE_TIMEOUT,
    NETWORK_DIAGNOSTICS_READ_FAILED,
    NETWORK_DIAGNOSTICS_READ_TIMEOUT,
    NETWORK_DIAGNOSTICS_INVALID_RESPONSE,
    NETWORK_DIAGNOSTICS_NO_MEMORY,
    NETWORK_DIAGNOSTICS_BUSY,
    NETWORK_DIAGNOSTICS_CANCELLED,
    NETWORK_DIAGNOSTICS_TOTAL_TIMEOUT,
    NETWORK_DIAGNOSTICS_UPSTREAM_FAILED,
    NETWORK_DIAGNOSTICS_BASIC_ONLY,
    NETWORK_DIAGNOSTICS_INVALID_TARGET,
} network_diagnostics_reason_t;

typedef struct {
    network_diagnostics_state_t state;
    network_diagnostics_reason_t reason;
} network_diagnostics_item_t;

typedef struct {
    network_diagnostics_item_t wifi, ip, time, dns, https;
    int rssi;
    char ipv4[16];
    char resolved_ipv4[16];
    char host[48];
    int http_status; /* A response is reachability evidence, not business API success. */
    int transport_error;
    uint32_t certificate_flags;
    uint32_t elapsed_ms;
    size_t free_internal_heap;
    size_t largest_internal_block;
    bool complete;
    bool cancelled;
} network_diagnostics_result_t;

typedef struct {
    const char *url; /* Only https://interfacepc.music.163.com/ or https://u.y.qq.com/ */
    uint32_t dns_timeout_ms;     /* 0: 4000; values capped at 10000. */
    uint32_t connect_timeout_ms; /* 0: 7000; values capped at 10000. */
    uint32_t read_timeout_ms;    /* 0: 4000; values capped at 10000. */
    uint32_t total_timeout_ms;   /* 0: 18000; values capped at 30000. */
} network_diagnostics_config_t;

typedef struct {
    /* Both callbacks run on the caller's worker. Copy snapshots before returning.
     * Keep callbacks short; they must not retain the result pointer. */
    void (*publish)(const network_diagnostics_result_t *result, void *context);
    bool (*is_cancelled)(void *context);
    void *context;
} network_diagnostics_callbacks_t;

/* Basic state only: no DNS/TLS, no allocations, never changes device settings. */
void network_diagnostics_collect_basic(network_diagnostics_result_t *result);

/* Worker-only synchronous operation. Caller owns config, callbacks and result for
 * the duration. No LVGL calls; no credentials; HEAD request with no redirects.
 * Every network wait is bounded, cancellation checked between short polls.
 * The return value is the final reason; use per-item states for partial results. */
network_diagnostics_reason_t network_diagnostics_run(
    const network_diagnostics_config_t *config,
    const network_diagnostics_callbacks_t *callbacks,
    network_diagnostics_result_t *result);

/* Six display lines: Wi-Fi, IPv4, time, DNS, HTTPS, scope/reason. */
void network_diagnostics_format_line(const network_diagnostics_result_t *result,
                                     unsigned line, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
