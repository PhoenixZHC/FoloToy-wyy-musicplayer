#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
typedef struct { int rssi; } wifi_ap_record_t;
typedef struct { uint32_t addr; } ip_addr_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { int unused; } esp_netif_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((uint8_t *)&(ip)->addr)[0], ((uint8_t *)&(ip)->addr)[1], ((uint8_t *)&(ip)->addr)[2], ((uint8_t *)&(ip)->addr)[3]
#define IP_IS_V4(ip) (true)
#define ip_2_ip4(ip) (ip)
#define ip4_addr_isany_val(ip) ((ip).addr == 0)
char *ipaddr_ntoa_r(const ip_addr_t *, char *, int);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *);
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
int64_t esp_timer_get_time(void);
void esp_fill_random(void *, size_t);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
typedef int portMUX_TYPE;
typedef uint32_t TickType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(TickType_t);
typedef int err_t;
#define ERR_OK 0
#define ERR_INPROGRESS -5
#define ERR_VAL -6
#define ERR_MEM -1
#define LWIP_DNS_ADDRTYPE_IPV4 0
typedef void (*dns_found_callback)(const char *, const ip_addr_t *, void *);
typedef void (*tcpip_callback_fn)(void *);
err_t tcpip_try_callback(tcpip_callback_fn, void *);
err_t dns_gethostbyname_addrtype(const char *, ip_addr_t *, dns_found_callback, void *, uint8_t);

typedef struct {
    void *bio;
    int (*send_fn)(void *, const unsigned char *, size_t);
    int (*receive)(void *, unsigned char *, size_t);
} mbedtls_ssl_context;
typedef struct { int auth; } mbedtls_ssl_config;
#define MBEDTLS_SSL_IS_CLIENT 0
#define MBEDTLS_SSL_TRANSPORT_STREAM 0
#define MBEDTLS_SSL_PRESET_DEFAULT 0
#define MBEDTLS_SSL_VERIFY_REQUIRED 2
#define MBEDTLS_ERR_SSL_WANT_READ -0x6900
#define MBEDTLS_ERR_SSL_WANT_WRITE -0x6880
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004e
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004c
#define MBEDTLS_ERR_SSL_ALLOC_FAILED -0x7f00
#define MBEDTLS_ERR_X509_ALLOC_FAILED -0x2880
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED -0x2700
#define MBEDTLS_ERR_ASN1_ALLOC_FAILED -0x006a
#define MBEDTLS_ERR_MPI_ALLOC_FAILED -0x0010
#define MBEDTLS_ERR_ECP_ALLOC_FAILED -0x4d80
#define MBEDTLS_ERR_PK_ALLOC_FAILED -0x3f80
void mbedtls_ssl_init(mbedtls_ssl_context *);
void mbedtls_ssl_config_init(mbedtls_ssl_config *);
void mbedtls_ssl_free(mbedtls_ssl_context *);
void mbedtls_ssl_config_free(mbedtls_ssl_config *);
int mbedtls_ssl_config_defaults(mbedtls_ssl_config *, int, int, int);
void mbedtls_ssl_conf_authmode(mbedtls_ssl_config *, int);
void mbedtls_ssl_conf_rng(mbedtls_ssl_config *, int (*)(void *, unsigned char *, size_t), void *);
int mbedtls_ssl_setup(mbedtls_ssl_context *, const mbedtls_ssl_config *);
int mbedtls_ssl_set_hostname(mbedtls_ssl_context *, const char *);
void mbedtls_ssl_set_bio(mbedtls_ssl_context *, void *, int (*)(void *, const unsigned char *, size_t), int (*)(void *, unsigned char *, size_t), void *);
int mbedtls_ssl_handshake(mbedtls_ssl_context *);
int mbedtls_ssl_write(mbedtls_ssl_context *, const unsigned char *, size_t);
int mbedtls_ssl_read(mbedtls_ssl_context *, unsigned char *, size_t);
uint32_t mbedtls_ssl_get_verify_result(const mbedtls_ssl_context *);
int esp_crt_bundle_attach(void *);

/* Never open host network sockets: substitutes capture the actual production
 * module's request and emulate short reads/writes and polling deadlines. */
#define socket nd_socket
#define connect nd_connect
#define select nd_select
#define getsockopt nd_getsockopt
#define send nd_send
#define recv nd_recv
#define close nd_close
#define fcntl nd_fcntl
#define time(...) nd_time(__VA_ARGS__)
int nd_socket(int, int, int);
int nd_connect(int, const struct sockaddr *, socklen_t);
int nd_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int nd_getsockopt(int, int, int, void *, socklen_t *);
ssize_t nd_send(int, const void *, size_t, int);
ssize_t nd_recv(int, void *, size_t, int);
int nd_close(int);
int nd_fcntl(int, int, ...);
time_t nd_time(time_t *);
