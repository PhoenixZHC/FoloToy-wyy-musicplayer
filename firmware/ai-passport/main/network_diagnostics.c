#include "network_diagnostics.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/asn1.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

#define POLL_MS 20
#define DNS_SLOTS 2
#define MIN_FREE_HEAP (64 * 1024)
#define MIN_HEAP_BLOCK (18 * 1024)

typedef struct {
    bool used, worker, callback, done;
    err_t error;
    char host[48];
    ip_addr_t address;
} dns_job_t;

/* 槽位的生命周期独立于页面和 worker。放弃请求后，lwIP 回调仍拥有槽位；
 * 只有 worker 和回调双方均结束，才允许下一次请求复用该地址。 */
static dns_job_t s_dns_jobs[DNS_SLOTS];
static portMUX_TYPE s_guard = portMUX_INITIALIZER_UNLOCKED;
static bool s_running;

typedef struct {
    const network_diagnostics_callbacks_t *callbacks;
    network_diagnostics_result_t *result;
    int64_t start_ms, end_ms;
} run_t;

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    int socket;
} tls_session_t;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void sample_heap(network_diagnostics_result_t *result)
{
    result->free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    result->largest_internal_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void item(network_diagnostics_item_t *value, network_diagnostics_state_t state,
                 network_diagnostics_reason_t reason)
{
    value->state = state;
    value->reason = reason;
}

void network_diagnostics_collect_basic(network_diagnostics_result_t *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        result->rssi = ap.rssi;
        item(&result->wifi, NETWORK_DIAGNOSTICS_PASS, NETWORK_DIAGNOSTICS_OK);
    } else {
        item(&result->wifi, NETWORK_DIAGNOSTICS_FAIL, NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED);
    }
    esp_netif_ip_info_t info = {0};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (result->wifi.state == NETWORK_DIAGNOSTICS_PASS && netif &&
        esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr) {
        snprintf(result->ipv4, sizeof(result->ipv4), IPSTR, IP2STR(&info.ip));
        item(&result->ip, NETWORK_DIAGNOSTICS_PASS, NETWORK_DIAGNOSTICS_OK);
    } else {
        item(&result->ip, NETWORK_DIAGNOSTICS_FAIL, NETWORK_DIAGNOSTICS_NO_IP);
    }
    time_t epoch = time(NULL);
    bool valid = epoch >= (time_t)1704067200 && (int64_t)epoch < INT64_C(4102444800);
    item(&result->time, valid ? NETWORK_DIAGNOSTICS_PASS : NETWORK_DIAGNOSTICS_FAIL,
         valid ? NETWORK_DIAGNOSTICS_OK : NETWORK_DIAGNOSTICS_TIME_INVALID);
    item(&result->dns, NETWORK_DIAGNOSTICS_NOT_RUN, NETWORK_DIAGNOSTICS_BASIC_ONLY);
    item(&result->https, NETWORK_DIAGNOSTICS_NOT_RUN, NETWORK_DIAGNOSTICS_BASIC_ONLY);
    sample_heap(result);
    result->complete = true;
}

static void publish(run_t *run)
{
    run->result->elapsed_ms = (uint32_t)(now_ms() - run->start_ms);
    if (run->callbacks && run->callbacks->publish)
        run->callbacks->publish(run->result, run->callbacks->context);
}

static network_diagnostics_reason_t interruption(run_t *run)
{
    if (run->callbacks && run->callbacks->is_cancelled &&
        run->callbacks->is_cancelled(run->callbacks->context))
        return NETWORK_DIAGNOSTICS_CANCELLED;
    if (now_ms() >= run->end_ms) return NETWORK_DIAGNOSTICS_TOTAL_TIMEOUT;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        item(&run->result->wifi, NETWORK_DIAGNOSTICS_FAIL, NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED);
        item(&run->result->ip, NETWORK_DIAGNOSTICS_FAIL, NETWORK_DIAGNOSTICS_NO_IP);
        run->result->ipv4[0] = '\0';
        return NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED;
    }
    return NETWORK_DIAGNOSTICS_OK;
}

static network_diagnostics_reason_t wait_status(run_t *run, int64_t deadline,
                                               network_diagnostics_reason_t timeout)
{
    network_diagnostics_reason_t reason = interruption(run);
    if (reason != NETWORK_DIAGNOSTICS_OK) return reason;
    return now_ms() >= deadline ? timeout : NETWORK_DIAGNOSTICS_OK;
}

static void pause_poll(void)
{
    TickType_t ticks = pdMS_TO_TICKS(POLL_MS);
    vTaskDelay(ticks ? ticks : 1);
}

static void dns_release_locked(dns_job_t *job)
{
    if (!job->worker && !job->callback) job->used = false;
}

static void dns_found(const char *name, const ip_addr_t *address, void *context)
{
    (void)name;
    dns_job_t *job = context;
    taskENTER_CRITICAL(&s_guard);
    if (address && IP_IS_V4(address) && !ip4_addr_isany_val(*ip_2_ip4(address))) {
        job->address = *address;
        job->error = ERR_OK;
    } else {
        job->error = ERR_VAL;
    }
    job->done = true;
    job->callback = false;
    dns_release_locked(job);
    taskEXIT_CRITICAL(&s_guard);
}

static void dns_start(void *context)
{
    dns_job_t *job = context;
    taskENTER_CRITICAL(&s_guard);
    bool abandoned = !job->worker;
    if (abandoned) {
        job->callback = false;
        dns_release_locked(job);
    }
    taskEXIT_CRITICAL(&s_guard);
    if (abandoned) return;

    ip_addr_t address;
    err_t error = dns_gethostbyname_addrtype(job->host, &address, dns_found, job, LWIP_DNS_ADDRTYPE_IPV4);
    if (error == ERR_INPROGRESS) return;
    /* 同步命中缓存时，lwIP 不再调用 dns_found。 */
    if (error == ERR_OK) {
        dns_found(job->host, &address, job);
    } else {
        taskENTER_CRITICAL(&s_guard);
        job->error = error;
        job->done = true;
        job->callback = false;
        dns_release_locked(job);
        taskEXIT_CRITICAL(&s_guard);
    }
}

static network_diagnostics_reason_t resolve(run_t *run, uint32_t timeout)
{
    dns_job_t *job = NULL;
    taskENTER_CRITICAL(&s_guard);
    for (unsigned i = 0; i < DNS_SLOTS; ++i) {
        if (s_dns_jobs[i].used) continue;
        job = &s_dns_jobs[i];
        memset(job, 0, sizeof(*job));
        job->used = job->worker = job->callback = true;
        snprintf(job->host, sizeof(job->host), "%s", run->result->host);
        break;
    }
    taskEXIT_CRITICAL(&s_guard);
    if (!job) return NETWORK_DIAGNOSTICS_BUSY;
    err_t queued = tcpip_try_callback(dns_start, job);
    if (queued != ERR_OK) {
        taskENTER_CRITICAL(&s_guard);
        job->callback = job->worker = false;
        dns_release_locked(job);
        taskEXIT_CRITICAL(&s_guard);
        return queued == ERR_MEM ? NETWORK_DIAGNOSTICS_NO_MEMORY : NETWORK_DIAGNOSTICS_DNS_FAILED;
    }
    int64_t deadline = now_ms() + timeout;
    network_diagnostics_reason_t reason;
    for (;;) {
        reason = wait_status(run, deadline, NETWORK_DIAGNOSTICS_DNS_TIMEOUT);
        if (reason != NETWORK_DIAGNOSTICS_OK) break;
        taskENTER_CRITICAL(&s_guard);
        bool done = job->done;
        err_t error = job->error;
        ip_addr_t address = job->address;
        taskEXIT_CRITICAL(&s_guard);
        if (done) {
            reason = error == ERR_OK ? NETWORK_DIAGNOSTICS_OK :
                     error == ERR_MEM ? NETWORK_DIAGNOSTICS_NO_MEMORY : NETWORK_DIAGNOSTICS_DNS_FAILED;
            if (reason == NETWORK_DIAGNOSTICS_OK &&
                !ipaddr_ntoa_r(&address, run->result->resolved_ipv4, sizeof(run->result->resolved_ipv4)))
                reason = NETWORK_DIAGNOSTICS_DNS_FAILED;
            break;
        }
        pause_poll();
    }
    taskENTER_CRITICAL(&s_guard);
    job->worker = false;
    dns_release_locked(job);
    taskEXIT_CRITICAL(&s_guard);
    return reason;
}

static int random_bytes(void *context, unsigned char *data, size_t length)
{
    (void)context;
    /* 完整检测必须保持 Wi-Fi 已连接；活动射频提供硬件随机源熵。 */
    esp_fill_random(data, length);
    return 0;
}

static int tls_send(void *context, const unsigned char *data, size_t length)
{
    int sent = send(*(int *)context, data, length, 0);
    if (sent >= 0) return sent;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ?
           MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_receive(void *context, unsigned char *data, size_t length)
{
    int received = recv(*(int *)context, data, length, 0);
    if (received >= 0) return received;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ?
           MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

static bool retry_tls(int value)
{
    return value == MBEDTLS_ERR_SSL_WANT_READ || value == MBEDTLS_ERR_SSL_WANT_WRITE;
}

static network_diagnostics_reason_t tls_error(run_t *run, tls_session_t *tls, int error)
{
    run->result->transport_error = error;
    uint32_t flags = mbedtls_ssl_get_verify_result(&tls->ssl);
    /* UINT32_MAX 表示尚无校验结果，不应把初始化/协议错误报成证书错误。 */
    run->result->certificate_flags = flags == UINT32_MAX ? 0 : flags;
    unsigned code = error < 0 ? (unsigned)-error : (unsigned)error;
    unsigned high = code & ~0x7fu, low = code & 0x7fu;
    if (high == (unsigned)-MBEDTLS_ERR_SSL_ALLOC_FAILED || high == (unsigned)-MBEDTLS_ERR_X509_ALLOC_FAILED ||
        high == (unsigned)-MBEDTLS_ERR_PK_ALLOC_FAILED || high == (unsigned)-MBEDTLS_ERR_ECP_ALLOC_FAILED ||
        low == (unsigned)-MBEDTLS_ERR_MPI_ALLOC_FAILED || low == (unsigned)-MBEDTLS_ERR_ASN1_ALLOC_FAILED)
        return NETWORK_DIAGNOSTICS_NO_MEMORY;
    if (run->result->certificate_flags || error == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
        return NETWORK_DIAGNOSTICS_CERTIFICATE_FAILED;
    return NETWORK_DIAGNOSTICS_TLS_FAILED;
}

static network_diagnostics_reason_t tcp_connect(run_t *run, tls_session_t *tls, int64_t deadline)
{
    tls->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tls->socket < 0) {
        run->result->transport_error = errno;
        return errno == ENOMEM || errno == ENOBUFS ? NETWORK_DIAGNOSTICS_NO_MEMORY : NETWORK_DIAGNOSTICS_TLS_FAILED;
    }
    int flags = fcntl(tls->socket, F_GETFL, 0);
    if (flags < 0 || fcntl(tls->socket, F_SETFL, flags | O_NONBLOCK) < 0)
        return NETWORK_DIAGNOSTICS_TLS_FAILED;
    struct sockaddr_in remote = {.sin_family = AF_INET, .sin_port = htons(443)};
    if (inet_pton(AF_INET, run->result->resolved_ipv4, &remote.sin_addr) != 1)
        return NETWORK_DIAGNOSTICS_DNS_FAILED;
    int status = connect(tls->socket, (struct sockaddr *)&remote, sizeof(remote));
    if (status == 0) return NETWORK_DIAGNOSTICS_OK;
    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        run->result->transport_error = errno;
        return NETWORK_DIAGNOSTICS_TLS_FAILED;
    }
    for (;;) {
        network_diagnostics_reason_t reason = wait_status(run, deadline, NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT);
        if (reason != NETWORK_DIAGNOSTICS_OK) return reason;
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(tls->socket, &writable);
        struct timeval zero = {0};
        status = select(tls->socket + 1, NULL, &writable, NULL, &zero);
        if (status < 0 && errno != EINTR) return NETWORK_DIAGNOSTICS_TLS_FAILED;
        if (status > 0) {
            int error = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(tls->socket, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && !error)
                return NETWORK_DIAGNOSTICS_OK;
            run->result->transport_error = error ? error : errno;
            return NETWORK_DIAGNOSTICS_TLS_FAILED;
        }
        pause_poll();
    }
}

static network_diagnostics_reason_t https_probe(run_t *run, const char *path,
                                               uint32_t connect_timeout, uint32_t read_timeout)
{
    sample_heap(run->result);
    if (run->result->free_internal_heap < MIN_FREE_HEAP ||
        run->result->largest_internal_block < MIN_HEAP_BLOCK)
        return NETWORK_DIAGNOSTICS_NO_MEMORY;
    tls_session_t *tls = calloc(1, sizeof(*tls));
    if (!tls) return NETWORK_DIAGNOSTICS_NO_MEMORY;
    tls->socket = -1;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    int64_t deadline = now_ms() + connect_timeout;
    network_diagnostics_reason_t reason = tcp_connect(run, tls, deadline);
    int status = 0;
    if (reason != NETWORK_DIAGNOSTICS_OK) goto cleanup;
    status = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (status) { reason = tls_error(run, tls, status); goto cleanup; }
    mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&tls->config, random_bytes, NULL);
    esp_err_t bundle_error = esp_crt_bundle_attach(&tls->config);
    if (bundle_error != ESP_OK) {
        run->result->transport_error = bundle_error;
        reason = bundle_error == ESP_ERR_NO_MEM ? NETWORK_DIAGNOSTICS_NO_MEMORY : NETWORK_DIAGNOSTICS_CERTIFICATE_FAILED;
        goto cleanup;
    }
    status = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    if (!status) status = mbedtls_ssl_set_hostname(&tls->ssl, run->result->host);
    if (status) { reason = tls_error(run, tls, status); goto cleanup; }
    mbedtls_ssl_set_bio(&tls->ssl, &tls->socket, tls_send, tls_receive, NULL);
    for (;;) {
        reason = wait_status(run, deadline, NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT);
        if (reason != NETWORK_DIAGNOSTICS_OK) goto cleanup;
        status = mbedtls_ssl_handshake(&tls->ssl);
        if (!status) break;
        if (!retry_tls(status)) { reason = tls_error(run, tls, status); goto cleanup; }
        pause_poll();
    }
    /* 限定只读 HEAD，不发送账号、cookie、请求体，不跟随重定向。 */
    char request[256];
    int length = snprintf(request, sizeof(request), "HEAD %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: FoloMusic-NetworkTest\r\n\r\n", path, run->result->host);
    if (length < 0 || (size_t)length >= sizeof(request)) {
        reason = NETWORK_DIAGNOSTICS_INVALID_TARGET;
        goto cleanup;
    }
    deadline = now_ms() + read_timeout;
    size_t sent = 0;
    while (sent < (size_t)length) {
        reason = wait_status(run, deadline, NETWORK_DIAGNOSTICS_WRITE_TIMEOUT);
        if (reason != NETWORK_DIAGNOSTICS_OK) goto cleanup;
        status = mbedtls_ssl_write(&tls->ssl, (unsigned char *)request + sent, (size_t)length - sent);
        if (status > 0) sent += (size_t)status;
        else if (!retry_tls(status)) {
            run->result->transport_error = status;
            reason = NETWORK_DIAGNOSTICS_WRITE_FAILED;
            goto cleanup;
        } else pause_poll();
    }
    /* 只读取有界状态行，任何合法 HTTP 响应均证明 HTTPS 服务可达。 */
    char response[96] = {0};
    size_t used = 0;
    deadline = now_ms() + read_timeout;
    for (;;) {
        reason = wait_status(run, deadline, NETWORK_DIAGNOSTICS_READ_TIMEOUT);
        if (reason != NETWORK_DIAGNOSTICS_OK) goto cleanup;
        status = mbedtls_ssl_read(&tls->ssl, (unsigned char *)response + used, sizeof(response) - 1 - used);
        if (status > 0) {
            used += (size_t)status;
            response[used] = '\0';
            char *line_end = memchr(response, '\n', used);
            if (line_end) {
                /* 固定位置解析，避免 sscanf 对超长数字的溢出行为。 */
                if (line_end - response < 13 ||
                    (memcmp(response, "HTTP/1.1 ", 9) && memcmp(response, "HTTP/1.0 ", 9)) ||
                    response[9] < '1' || response[9] > '5' ||
                    response[10] < '0' || response[10] > '9' ||
                    response[11] < '0' || response[11] > '9' || response[12] != ' ' ||
                    line_end[-1] != '\r') {
                    reason = NETWORK_DIAGNOSTICS_INVALID_RESPONSE;
                } else {
                    run->result->http_status = (response[9] - '0') * 100 + (response[10] - '0') * 10 + response[11] - '0';
                }
                goto cleanup;
            }
            if (used == sizeof(response) - 1) { reason = NETWORK_DIAGNOSTICS_INVALID_RESPONSE; goto cleanup; }
        } else if (!retry_tls(status)) {
            run->result->transport_error = status;
            reason = status == 0 ? NETWORK_DIAGNOSTICS_INVALID_RESPONSE : NETWORK_DIAGNOSTICS_READ_FAILED;
            goto cleanup;
        } else pause_poll();
    }
cleanup:
    /* 不等待 close_notify 或对端关闭；没有请求体/响应体缓存需排空。 */
    if (tls->socket >= 0) close(tls->socket);
    mbedtls_ssl_free(&tls->ssl);
    mbedtls_ssl_config_free(&tls->config);
    free(tls);
    return reason;
}

static bool parse_target(const char *url, char *host, size_t capacity, const char **path)
{
    static const char *const hosts[] = {"interfacepc.music.163.com", "u.y.qq.com"};
    if (!url || strncmp(url, "https://", 8)) return false;
    const char *start = url + 8;
    const char *slash = strchr(start, '/');
    size_t length = slash ? (size_t)(slash - start) : strlen(start);
    bool allowed = false;
    for (unsigned i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
        if (strlen(hosts[i]) == length && !memcmp(start, hosts[i], length)) allowed = true;
    if (!allowed || length >= capacity) return false;
    /* 第一版只探测根路径；接口参数可能触发业务行为或携带凭据。 */
    if (slash && strcmp(slash, "/")) return false;
    memcpy(host, start, length);
    host[length] = '\0';
    *path = "/";
    return true;
}

static uint32_t timeout_value(uint32_t value, uint32_t fallback, uint32_t maximum)
{
    return !value ? fallback : value > maximum ? maximum : value;
}

network_diagnostics_reason_t network_diagnostics_run(
    const network_diagnostics_config_t *config,
    const network_diagnostics_callbacks_t *callbacks,
    network_diagnostics_result_t *result)
{
    if (!result) return NETWORK_DIAGNOSTICS_INVALID_TARGET;
    network_diagnostics_collect_basic(result);
    run_t run = {.callbacks = callbacks, .result = result, .start_ms = now_ms()};
    run.end_ms = run.start_ms + timeout_value(config ? config->total_timeout_ms : 0, 18000, 30000);
    result->complete = false;
    item(&result->dns, NETWORK_DIAGNOSTICS_NOT_RUN, NETWORK_DIAGNOSTICS_UPSTREAM_FAILED);
    item(&result->https, NETWORK_DIAGNOSTICS_NOT_RUN, NETWORK_DIAGNOSTICS_UPSTREAM_FAILED);
    const char *path = NULL;
    network_diagnostics_reason_t reason = NETWORK_DIAGNOSTICS_OK;
    bool owns_run = false;
    if (!config || !parse_target(config->url, result->host, sizeof(result->host), &path)) {
        reason = NETWORK_DIAGNOSTICS_INVALID_TARGET;
        goto finish;
    }
    taskENTER_CRITICAL(&s_guard);
    if (!s_running) owns_run = s_running = true;
    taskEXIT_CRITICAL(&s_guard);
    if (!owns_run) { reason = NETWORK_DIAGNOSTICS_BUSY; goto finish; }
    publish(&run);
    if (result->wifi.state != NETWORK_DIAGNOSTICS_PASS) { reason = NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED; goto finish; }
    if (result->ip.state != NETWORK_DIAGNOSTICS_PASS) { reason = NETWORK_DIAGNOSTICS_NO_IP; goto finish; }
    reason = interruption(&run);
    if (reason != NETWORK_DIAGNOSTICS_OK) goto finish;
    item(&result->dns, NETWORK_DIAGNOSTICS_RUNNING, NETWORK_DIAGNOSTICS_OK);
    publish(&run);
    reason = resolve(&run, timeout_value(config->dns_timeout_ms, 4000, 10000));
    item(&result->dns, reason == NETWORK_DIAGNOSTICS_OK ? NETWORK_DIAGNOSTICS_PASS : NETWORK_DIAGNOSTICS_FAIL, reason);
    publish(&run);
    if (reason != NETWORK_DIAGNOSTICS_OK) goto finish;
    if (result->time.state != NETWORK_DIAGNOSTICS_PASS) { reason = NETWORK_DIAGNOSTICS_TIME_INVALID; goto finish; }
    reason = interruption(&run);
    if (reason != NETWORK_DIAGNOSTICS_OK) goto finish;
    item(&result->https, NETWORK_DIAGNOSTICS_RUNNING, NETWORK_DIAGNOSTICS_OK);
    publish(&run);
    reason = https_probe(&run, path, timeout_value(config->connect_timeout_ms, 7000, 10000),
                         timeout_value(config->read_timeout_ms, 4000, 10000));
    item(&result->https, reason == NETWORK_DIAGNOSTICS_OK ? NETWORK_DIAGNOSTICS_PASS : NETWORK_DIAGNOSTICS_FAIL, reason);
finish:
    if (result->dns.state == NETWORK_DIAGNOSTICS_NOT_RUN) result->dns.reason = reason;
    if (result->https.state == NETWORK_DIAGNOSTICS_NOT_RUN) result->https.reason = reason;
    result->cancelled = reason == NETWORK_DIAGNOSTICS_CANCELLED;
    result->complete = true;
    if (owns_run) {
        taskENTER_CRITICAL(&s_guard);
        s_running = false;
        taskEXIT_CRITICAL(&s_guard);
    }
    publish(&run);
    return reason;
}

static const char *reason_text(network_diagnostics_reason_t reason)
{
    switch (reason) {
    case NETWORK_DIAGNOSTICS_OK: return "通过";
    case NETWORK_DIAGNOSTICS_WIFI_DISCONNECTED: return "Wi-Fi 未连接";
    case NETWORK_DIAGNOSTICS_NO_IP: return "未取得 IP";
    case NETWORK_DIAGNOSTICS_TIME_INVALID: return "系统时间无效";
    case NETWORK_DIAGNOSTICS_DNS_FAILED: return "域名解析失败";
    case NETWORK_DIAGNOSTICS_DNS_TIMEOUT: return "域名解析超时";
    case NETWORK_DIAGNOSTICS_TLS_FAILED: return "安全连接失败";
    case NETWORK_DIAGNOSTICS_CERTIFICATE_FAILED: return "证书校验失败";
    case NETWORK_DIAGNOSTICS_CONNECT_TIMEOUT: return "连接或握手超时";
    case NETWORK_DIAGNOSTICS_WRITE_FAILED: return "请求发送失败";
    case NETWORK_DIAGNOSTICS_WRITE_TIMEOUT: return "请求发送超时";
    case NETWORK_DIAGNOSTICS_READ_FAILED: return "响应读取失败";
    case NETWORK_DIAGNOSTICS_READ_TIMEOUT: return "响应读取超时";
    case NETWORK_DIAGNOSTICS_INVALID_RESPONSE: return "响应格式无效";
    case NETWORK_DIAGNOSTICS_NO_MEMORY: return "内存不足，请停止播放后重试";
    case NETWORK_DIAGNOSTICS_BUSY: return "检测资源忙，请稍后重试";
    case NETWORK_DIAGNOSTICS_CANCELLED: return "检测已取消";
    case NETWORK_DIAGNOSTICS_TOTAL_TIMEOUT: return "整轮检测超时";
    case NETWORK_DIAGNOSTICS_UPSTREAM_FAILED: return "前项未通过";
    case NETWORK_DIAGNOSTICS_BASIC_ONLY: return "请先暂停播放再检测";
    case NETWORK_DIAGNOSTICS_INVALID_TARGET: return "检测目标无效";
    }
    return "未知结果";
}

static const char *state_text(network_diagnostics_item_t value)
{
    if (value.state == NETWORK_DIAGNOSTICS_RUNNING) return "检测中";
    if (value.state == NETWORK_DIAGNOSTICS_NOT_RUN) return "未执行";
    return reason_text(value.reason);
}

void network_diagnostics_format_line(const network_diagnostics_result_t *result,
                                     unsigned line, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    out[0] = '\0';
    if (!result) return;
    switch (line) {
    case 0:
        if (result->wifi.state == NETWORK_DIAGNOSTICS_PASS)
            snprintf(out, capacity, "Wi-Fi: 已连接 %d dBm", result->rssi);
        else snprintf(out, capacity, "Wi-Fi: %s", state_text(result->wifi));
        break;
    case 1: snprintf(out, capacity, "IP: %s", result->ip.state == NETWORK_DIAGNOSTICS_PASS ? result->ipv4 : state_text(result->ip)); break;
    case 2: snprintf(out, capacity, "时间: %s", state_text(result->time)); break;
    case 3: snprintf(out, capacity, "DNS: %s", state_text(result->dns)); break;
    case 4:
        if (result->https.state == NETWORK_DIAGNOSTICS_PASS)
            snprintf(out, capacity, "HTTPS: 已响应 HTTP %d", result->http_status);
        else snprintf(out, capacity, "HTTPS: %s", state_text(result->https));
        break;
    case 5:
        if (result->https.state == NETWORK_DIAGNOSTICS_PASS)
            snprintf(out, capacity, "仅验证服务可达；未验证登录、权限或音频");
        else if (result->https.state == NETWORK_DIAGNOSTICS_NOT_RUN)
            snprintf(out, capacity, "%s", reason_text(result->https.reason));
        else if (result->https.state == NETWORK_DIAGNOSTICS_RUNNING)
            snprintf(out, capacity, "检测中；仅验证服务可达");
        else snprintf(out, capacity, "%s", reason_text(result->https.reason));
        break;
    default: break;
    }
}
