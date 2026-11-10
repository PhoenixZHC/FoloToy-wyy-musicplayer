#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QQMUSIC_QR_PNG_MAX 4096
typedef struct {
    char id[129];
    uint8_t png[QQMUSIC_QR_PNG_MAX];
    size_t png_length;
    unsigned expires_seconds;
} qqmusic_login_qr_t;
typedef enum {
    QQMUSIC_LOGIN_WAITING, QQMUSIC_LOGIN_SCANNED, QQMUSIC_LOGIN_CANCELED,
    QQMUSIC_LOGIN_EXPIRED, QQMUSIC_LOGIN_REJECTED, QQMUSIC_LOGIN_CONFIRMED
} qqmusic_login_state_t;
typedef struct qqmusic_login_connection qqmusic_login_connection_t;

/* 单worker API。不存储凭据；调用方核对会话代次后才能持久化。 */
esp_err_t qqmusic_login_create_qr(qqmusic_login_qr_t *qr);
esp_err_t qqmusic_login_connect(const char *qr_id,qqmusic_login_connection_t **out);
/* 单次等待最多约1秒。最终credential_json由调用方清零并free，不能记录到日志。 */
esp_err_t qqmusic_login_poll(qqmusic_login_connection_t *connection,
                           qqmusic_login_state_t *state,char **credential_json);
void qqmusic_login_close(qqmusic_login_connection_t *connection);
/* 订阅确认后才允许展示二维码，避免扫码事件先于接收订阅而丢失。 */
bool qqmusic_login_ready(const qqmusic_login_connection_t *connection);
