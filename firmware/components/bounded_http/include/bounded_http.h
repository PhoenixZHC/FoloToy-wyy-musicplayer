#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_client.h"

/* 配置is_async=true，event_handler设置为下列handler（或从已有handler转调用）。
 * 仅在尚未连接时重试CONNECTING/EAGAIN；连接后的部分header写失败绝不重试。
 * SDK DNS仍同步受resolver超时控制，不能承诺覆盖DNS的绝对整轮时限。 */
esp_err_t bounded_http_event_handler(esp_http_client_event_t *event);
esp_err_t bounded_http_open(esp_http_client_handle_t http,int64_t absolute_deadline_us);
/* Async connect leaves TLS nonblocking; restore blocking reads before headers/body. */
esp_err_t bounded_http_make_blocking(esp_http_client_handle_t http);

/* 单一串行worker：open成功后绑定已连接socket；busy必须作为请求失败处理。
 * 截止时只shutdown socket，不访问TLS对象；caller始终负责HTTP cleanup。
 * 每次cleanup前stop，即使请求中途失败。timer与上下文永久保留，避免迟到回调悬空。 */
esp_err_t bounded_http_start(esp_http_client_handle_t http,int64_t absolute_deadline_us);
bool bounded_http_expired(esp_http_client_handle_t http);
void bounded_http_stop(esp_http_client_handle_t http);
