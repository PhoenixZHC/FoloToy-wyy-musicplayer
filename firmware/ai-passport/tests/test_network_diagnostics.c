#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "network_diagnostics.h"
#include "platform.h"

typedef struct {
    dns_found_callback function;
    void *context;
    char host[48];
} pending_dns_t;
static pending_dns_t pending_dns[4];
static unsigned pending_count;
static tcpip_callback_fn pending_start;
static void *pending_start_context;
static bool defer_start;
static int64_t clock_ms, disconnect_at, cancel_at, complete_dns_at;
static bool connected, has_ip, valid_time, dns_hangs, dns_fails, tcp_hangs, read_hangs, write_hangs;
static int handshake_result, handshake_waits, handshake_calls, dns_calls, sockets_live, ssl_live;
static uint32_t cert_flags;
static size_t free_heap, largest_heap;
static const char *response;
static size_t response_offset, request_length;
static char request[512], certificate_host[48];
static network_diagnostics_result_t snapshots[16];
static unsigned publish_count;
static network_diagnostics_config_t config;

static void dns_complete_all(bool found)
{
    while (pending_count) {
        pending_dns_t saved = pending_dns[--pending_count];
        ip_addr_t address = {.addr = inet_addr("1.2.3.4")};
        saved.function(saved.host, found ? &address : NULL, saved.context);
    }
}

static void reset(void)
{
    assert(!pending_count && !pending_start && !sockets_live && !ssl_live);
    clock_ms = 0;
    disconnect_at = cancel_at = complete_dns_at = -1;
    connected = has_ip = valid_time = true;
    dns_hangs = dns_fails = tcp_hangs = read_hangs = write_hangs = defer_start = false;
    handshake_result = handshake_waits = handshake_calls = dns_calls = 0;
    cert_flags = 0;
    free_heap = 100 * 1024;
    largest_heap = 40 * 1024;
    response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    response_offset = request_length = publish_count = 0;
    request[0] = certificate_host[0] = '\0';
    config = (network_diagnostics_config_t){.url = "https://interfacepc.music.163.com/",
        .dns_timeout_ms = 100, .connect_timeout_ms = 140, .read_timeout_ms = 100, .total_timeout_ms = 500};
}

char *ipaddr_ntoa_r(const ip_addr_t *address, char *buffer, int length)
{
    return (char *)inet_ntop(AF_INET, &address->addr, buffer, (socklen_t)length);
}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{
    ap->rssi = -53;
    return connected && (disconnect_at < 0 || clock_ms < disconnect_at) ? ESP_OK : -1;
}
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    static esp_netif_t netif;
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return &netif;
}
int esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    assert(netif);
    info->ip.addr = has_ip ? inet_addr("192.168.4.22") : 0;
    return 0;
}
int64_t esp_timer_get_time(void) { return clock_ms * 1000; }
void esp_fill_random(void *buffer, size_t size) { memset(buffer, 0x5a, size); }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return free_heap; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return largest_heap; }
time_t nd_time(time_t *out)
{
    time_t value = valid_time ? 1794300000 : 100;
    if (out) *out = value;
    return value;
}
void vTaskDelay(TickType_t duration)
{
    clock_ms += duration;
    if (complete_dns_at >= 0 && clock_ms >= complete_dns_at) {
        complete_dns_at = -1;
        dns_complete_all(!dns_fails);
    }
}
err_t tcpip_try_callback(tcpip_callback_fn function, void *context)
{
    if (defer_start) {
        assert(!pending_start);
        pending_start = function;
        pending_start_context = context;
    } else function(context);
    return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char *host, ip_addr_t *address,
                               dns_found_callback function, void *context, uint8_t type)
{
    assert(type == LWIP_DNS_ADDRTYPE_IPV4);
    assert(!strcmp(host, "interfacepc.music.163.com") || !strcmp(host, "u.y.qq.com"));
    ++dns_calls;
    if (dns_fails) return ERR_VAL;
    if (dns_hangs) {
        assert(pending_count < 4);
        pending_dns_t *pending = &pending_dns[pending_count++];
        pending->function = function;
        pending->context = context;
        snprintf(pending->host, sizeof(pending->host), "%s", host);
        return ERR_INPROGRESS;
    }
    address->addr = inet_addr("1.2.3.4");
    return ERR_OK;
}

int nd_socket(int family, int type, int protocol)
{
    assert(family == AF_INET && type == SOCK_STREAM && protocol == IPPROTO_TCP);
    ++sockets_live;
    return 3;
}
int nd_fcntl(int socket, int command, ...)
{
    assert(socket == 3 && (command == F_GETFL || command == F_SETFL));
    return 0;
}
int nd_connect(int socket, const struct sockaddr *address, socklen_t length)
{
    assert(socket == 3 && length == sizeof(struct sockaddr_in));
    const struct sockaddr_in *ip = (const struct sockaddr_in *)address;
    assert(ip->sin_addr.s_addr == inet_addr("1.2.3.4") && ntohs(ip->sin_port) == 443);
    errno = EINPROGRESS;
    return -1;
}
int nd_select(int count, fd_set *read, fd_set *write, fd_set *except, struct timeval *timeout)
{
    assert(count == 4 && !read && write && !except && !timeout->tv_sec && !timeout->tv_usec);
    return tcp_hangs ? 0 : 1;
}
int nd_getsockopt(int socket, int level, int option, void *value, socklen_t *length)
{
    assert(socket == 3 && level == SOL_SOCKET && option == SO_ERROR && *length == sizeof(int));
    *(int *)value = 0;
    return 0;
}
ssize_t nd_send(int socket, const void *data, size_t length, int flags)
{
    assert(socket == 3 && !flags);
    if (write_hangs) { errno = EAGAIN; return -1; }
    size_t count = length < 7 ? length : 7;
    assert(request_length + count < sizeof(request));
    memcpy(request + request_length, data, count);
    request_length += count;
    request[request_length] = '\0';
    return (ssize_t)count;
}
ssize_t nd_recv(int socket, void *data, size_t length, int flags)
{
    assert(socket == 3 && !flags);
    if (read_hangs) { errno = EAGAIN; return -1; }
    size_t count = strlen(response) - response_offset;
    if (count > length) count = length;
    if (count > 3) count = 3;
    memcpy(data, response + response_offset, count);
    response_offset += count;
    return (ssize_t)count;
}
int nd_close(int socket) { assert(socket == 3 && sockets_live == 1); --sockets_live; return 0; }

void mbedtls_ssl_init(mbedtls_ssl_context *ssl) { memset(ssl, 0, sizeof(*ssl)); ++ssl_live; }
void mbedtls_ssl_config_init(mbedtls_ssl_config *cfg) { memset(cfg, 0, sizeof(*cfg)); }
void mbedtls_ssl_free(mbedtls_ssl_context *ssl) { (void)ssl; --ssl_live; }
void mbedtls_ssl_config_free(mbedtls_ssl_config *cfg) { (void)cfg; }
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *cfg, int endpoint, int transport, int preset)
{ (void)cfg; (void)endpoint; (void)transport; (void)preset; return 0; }
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *cfg, int mode)
{ assert(mode == MBEDTLS_SSL_VERIFY_REQUIRED); cfg->auth = mode; }
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *cfg, int (*function)(void *, unsigned char *, size_t), void *context)
{ (void)cfg; unsigned char bytes[8]; assert(!function(context, bytes, sizeof(bytes))); }
int esp_crt_bundle_attach(void *cfg)
{ assert(((mbedtls_ssl_config *)cfg)->auth == MBEDTLS_SSL_VERIFY_REQUIRED); return 0; }
int mbedtls_ssl_setup(mbedtls_ssl_context *ssl, const mbedtls_ssl_config *cfg)
{ (void)ssl; assert(cfg->auth == MBEDTLS_SSL_VERIFY_REQUIRED); return 0; }
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *ssl, const char *host)
{ (void)ssl; snprintf(certificate_host, sizeof(certificate_host), "%s", host); return 0; }
void mbedtls_ssl_set_bio(mbedtls_ssl_context *ssl, void *context,
    int (*send_fn)(void *, const unsigned char *, size_t),
    int (*receive)(void *, unsigned char *, size_t), void *timeout_fn)
{ assert(!timeout_fn); ssl->bio = context; ssl->send_fn = send_fn; ssl->receive = receive; }
int mbedtls_ssl_handshake(mbedtls_ssl_context *ssl)
{ assert(ssl->bio); return handshake_calls++ < handshake_waits ? MBEDTLS_ERR_SSL_WANT_READ : handshake_result; }
int mbedtls_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *data, size_t length)
{ return ssl->send_fn(ssl->bio, data, length); }
int mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *data, size_t length)
{ return ssl->receive(ssl->bio, data, length); }
uint32_t mbedtls_ssl_get_verify_result(const mbedtls_ssl_context *ssl) { (void)ssl; return cert_flags; }

static void published(const network_diagnostics_result_t *result, void *context)
{
    assert(context == &config);
    assert(publish_count < sizeof(snapshots) / sizeof(snapshots[0]));
    snapshots[publish_count++] = *result;
}
static bool cancelled(void *context) { assert(context == &config); return cancel_at >= 0 && clock_ms >= cancel_at; }
static network_diagnostics_result_t result;
static network_diagnostics_reason_t run(void)
{
    network_diagnostics_callbacks_t callbacks = {.publish = published, .is_cancelled = cancelled, .context = &config};
    network_diagnostics_reason_t reason = network_diagnostics_run(&config, &callbacks, &result);
    assert(result.complete && !sockets_live && !ssl_live);
    return reason;
}

int main(void)
{
    unsigned tests = 0;
    reset();
    network_diagnostics_collect_basic(&result);
    assert(result.wifi.state == NETWORK_DIAGNOSTICS_PASS && result.rssi == -53);
    assert(!strcmp(result.ipv4, "192.168.4.22") && result.time.state == NETWORK_DIAGNOSTICS_PASS);
    assert(result.dns.state == NETWORK_DIAGNOSTICS_NOT_RUN && !dns_calls && !request_length);
    ++tests;

    const char *urls[] = {"https://interfacepc.music.163.com/", "https://u.y.qq.com/"};
    for (unsigned i = 0; i < 2; ++i) {
        reset(); config.url = urls[i]; handshake_waits = 2;
        assert(run() == NETWORK_DIAGNOSTICS_OK);
        assert(result.dns.state == NETWORK_DIAGNOSTICS_PASS && result.https.state == NETWORK_DIAGNOSTICS_PASS);
        assert(!strcmp(result.resolved_ipv4, "1.2.3.4") && result.http_status == 200);
        assert(strstr(request, "HEAD / HTTP/1.1\r\nHost: ") == request);
        assert(strstr(request, certificate_host) && !strstr(request, "Cookie:") && !strstr(request, "Authorization:"));
        assert(publish_count == 5 && snapshots[1].dns.state == NETWORK_DIAGNOSTICS_RUNNING);
        assert(snapshots[3].https.state == NETWORK_DIAGNOSTICS_RUNNING && !snapshots[3].complete);
        assert(result.elapsed_ms == 40);
        ++tests;
    }

    reset(); connected = false;
    assert(run() == NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED && !dns_calls);
    assert(result.dns.state == NETWORK_DIAGNOSTICS_NOT_RUN && result.https.state == NETWORK_DIAGNOSTICS_NOT_RUN);
    ++tests;
    reset(); has_ip = false;
    assert(run() == NETWORK_DIAGNOSTICS_NO_IP && !dns_calls);
    ++tests;
    reset(); valid_time = false;
    assert(run() == NETWORK_DIAGNOSTICS_TIME_INVALID && dns_calls == 1);
    assert(result.dns.state == NETWORK_DIAGNOSTICS_PASS && result.https.state == NETWORK_DIAGNOSTICS_NOT_RUN);
    ++tests;
    reset(); dns_fails = true;
    assert(run() == NETWORK_DIAGNOSTICS_DNS_FAILED && !request_length);
    assert(result.https.state == NETWORK_DIAGNOSTICS_NOT_RUN);
    ++tests;

    reset(); dns_hangs = true; complete_dns_at = 40;
    assert(run() == NETWORK_DIAGNOSTICS_OK && clock_ms == 40 && !pending_count);
    assert(result.dns.state == NETWORK_DIAGNOSTICS_PASS);
    ++tests;

    reset(); dns_hangs = true;
    assert(run() == NETWORK_DIAGNOSTICS_DNS_TIMEOUT && clock_ms == 100);
    assert(result.dns.state == NETWORK_DIAGNOSTICS_FAIL && !request_length && pending_count == 1);
    /* Late callbacks after the result's lifetime must neither alter its result
     * nor corrupt a newly started request. Two outstanding slots remain bounded. */
    network_diagnostics_result_t old = result;
    publish_count = 0;
    assert(run() == NETWORK_DIAGNOSTICS_DNS_TIMEOUT && pending_count == 2);
    publish_count = 0;
    assert(run() == NETWORK_DIAGNOSTICS_BUSY && pending_count == 2);
    dns_complete_all(true);
    assert(old.dns.reason == NETWORK_DIAGNOSTICS_DNS_TIMEOUT);
    ++tests;

    reset(); dns_hangs = true; cancel_at = 40;
    assert(run() == NETWORK_DIAGNOSTICS_CANCELLED && result.cancelled && clock_ms == 40);
    old = result; dns_complete_all(true);
    assert(!memcmp(&old, &result, sizeof(result)));
    ++tests;

    reset(); defer_start = true; cancel_at = 40;
    assert(run() == NETWORK_DIAGNOSTICS_CANCELLED && !dns_calls);
    tcpip_callback_fn start = pending_start;
    pending_start = NULL;
    start(pending_start_context);
    assert(!dns_calls && !pending_count);
    ++tests;

    reset(); tcp_hangs = true;
    assert(run() == NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT && clock_ms == 140);
    ++tests;
    reset(); handshake_waits = 100;
    assert(run() == NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT && clock_ms == 140);
    ++tests;
    reset(); handshake_result = -0x7780;
    assert(run() == NETWORK_DIAGNOSTICS_TLS_FAILED && result.transport_error == -0x7780);
    ++tests;
    reset(); handshake_result = -0x2700; cert_flags = 0x8;
    assert(run() == NETWORK_DIAGNOSTICS_CERTIFICATE_FAILED && result.certificate_flags == 0x8);
    assert(result.time.state == NETWORK_DIAGNOSTICS_PASS);
    ++tests;
    reset(); handshake_result = MBEDTLS_ERR_SSL_ALLOC_FAILED;
    assert(run() == NETWORK_DIAGNOSTICS_NO_MEMORY);
    ++tests;
    reset(); handshake_result = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED + MBEDTLS_ERR_MPI_ALLOC_FAILED;
    assert(run() == NETWORK_DIAGNOSTICS_NO_MEMORY);
    ++tests;
    reset(); handshake_result = -0x7780; cert_flags = UINT32_MAX;
    assert(run() == NETWORK_DIAGNOSTICS_TLS_FAILED && !result.certificate_flags);
    ++tests;
    reset(); free_heap = 50 * 1024;
    assert(run() == NETWORK_DIAGNOSTICS_NO_MEMORY && !handshake_calls);
    ++tests;
    reset(); largest_heap = 12 * 1024;
    assert(run() == NETWORK_DIAGNOSTICS_NO_MEMORY && !handshake_calls);
    ++tests;
    reset(); response = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
    assert(run() == NETWORK_DIAGNOSTICS_OK && result.http_status == 503);
    assert(result.https.state == NETWORK_DIAGNOSTICS_PASS);
    char line[192];
    network_diagnostics_format_line(&result, 4, line, sizeof(line));
    assert(strstr(line, "已响应 HTTP 503"));
    network_diagnostics_format_line(&result, 5, line, sizeof(line));
    assert(strstr(line, "未验证登录"));
    ++tests;
    reset(); read_hangs = true;
    assert(run() == NETWORK_DIAGNOSTICS_READ_TIMEOUT && clock_ms == 100);
    ++tests;
    reset(); write_hangs = true;
    assert(run() == NETWORK_DIAGNOSTICS_WRITE_TIMEOUT && clock_ms == 100);
    ++tests;
    reset(); response = "HTTP/1.1 2000000000000000000000000000000000000 OK\r\n";
    assert(run() == NETWORK_DIAGNOSTICS_INVALID_RESPONSE);
    ++tests;
    reset(); tcp_hangs = true; disconnect_at = 60;
    assert(run() == NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED && clock_ms == 60);
    assert(result.wifi.state == NETWORK_DIAGNOSTICS_FAIL && !result.ipv4[0]);
    ++tests;
    reset(); read_hangs = true; cancel_at = 60;
    assert(run() == NETWORK_DIAGNOSTICS_CANCELLED && clock_ms == 60 && result.cancelled);
    ++tests;
    reset(); tcp_hangs = true; config.total_timeout_ms = 60;
    assert(run() == NETWORK_DIAGNOSTICS_TOTAL_TIMEOUT && clock_ms == 60);
    ++tests;
    reset(); config.url = "https://u.y.qq.com/music?cookie=secret";
    assert(run() == NETWORK_DIAGNOSTICS_INVALID_TARGET && !dns_calls && !request_length);
    ++tests;
    reset(); config.url = "https://u.y.qq.com.evil/";
    assert(run() == NETWORK_DIAGNOSTICS_INVALID_TARGET && !dns_calls);
    ++tests;
    reset(); config.url = "http://u.y.qq.com/";
    assert(run() == NETWORK_DIAGNOSTICS_INVALID_TARGET && !dns_calls);
    ++tests;

    /* Static DNS slots were recovered after all timeout/cancellation paths. */
    reset(); assert(run() == NETWORK_DIAGNOSTICS_OK); ++tests;
    printf("network diagnostics: %u passed, 0 failed\n", tests);
    return 0;
}
