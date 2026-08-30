#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "netease_official_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OFFICIAL_AUTH_LOGIN_QR = 0,
    OFFICIAL_AUTH_USE_ACCESS,
    OFFICIAL_AUTH_REFRESH,
    OFFICIAL_AUTH_CLEAR_AND_LOGIN_QR,
} official_auth_action_t;

official_auth_action_t official_auth_decide(const netease_token_set_t *tokens,
                                            int64_t now_ms,
                                            bool refresh_already_failed);
esp_err_t official_auth_load(netease_token_set_t *out);
esp_err_t official_auth_save(const netease_token_set_t *tokens);
esp_err_t official_auth_clear(void);

#ifdef __cplusplus
}
#endif
