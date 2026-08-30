#include "official_auth_store.h"

#include <string.h>

#define OFFICIAL_AUTH_REFRESH_MARGIN_MS (5 * 60 * 1000LL)

static bool token_text_present(const char *value)
{
    return value != NULL && value[0] != '\0';
}

official_auth_action_t official_auth_decide(const netease_token_set_t *tokens,
                                            int64_t now_ms,
                                            bool refresh_already_failed)
{
    if (tokens == NULL ||
        !token_text_present(tokens->access_token) ||
        !token_text_present(tokens->refresh_token)) {
        return OFFICIAL_AUTH_LOGIN_QR;
    }

    if (refresh_already_failed) {
        return OFFICIAL_AUTH_CLEAR_AND_LOGIN_QR;
    }

    if (tokens->refresh_expires_at_ms <= now_ms) {
        return OFFICIAL_AUTH_LOGIN_QR;
    }

    if (tokens->access_expires_at_ms > now_ms + OFFICIAL_AUTH_REFRESH_MARGIN_MS) {
        return OFFICIAL_AUTH_USE_ACCESS;
    }

    return OFFICIAL_AUTH_REFRESH;
}

#ifdef NETEASE_OFFICIAL_HOST_TEST
esp_err_t official_auth_load(netease_token_set_t *out)
{
    (void)out;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t official_auth_save(const netease_token_set_t *tokens)
{
    (void)tokens;
    return ESP_ERR_INVALID_STATE;
}

esp_err_t official_auth_clear(void)
{
    return ESP_OK;
}
#else
#include "nvs.h"

#define OFFICIAL_AUTH_NAMESPACE "netease_oauth"
#define OFFICIAL_AUTH_KEY_ACCESS "access"
#define OFFICIAL_AUTH_KEY_REFRESH "refresh"
#define OFFICIAL_AUTH_KEY_ACCESS_EXP "access_exp"
#define OFFICIAL_AUTH_KEY_REFRESH_EXP "refresh_exp"

static esp_err_t read_required_str(nvs_handle_t nvs, const char *key, char *out, size_t out_size)
{
    size_t required = out_size;
    esp_err_t err = nvs_get_str(nvs, key, out, &required);
    if (err != ESP_OK || required == 0 || required > out_size || out[0] == '\0') {
        if (out_size > 0) out[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t official_auth_load(netease_token_set_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OFFICIAL_AUTH_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return ESP_ERR_NOT_FOUND;

    err = read_required_str(nvs, OFFICIAL_AUTH_KEY_ACCESS, out->access_token, sizeof(out->access_token));
    if (err == ESP_OK) {
        err = read_required_str(nvs, OFFICIAL_AUTH_KEY_REFRESH, out->refresh_token, sizeof(out->refresh_token));
    }
    if (err == ESP_OK) {
        err = nvs_get_i64(nvs, OFFICIAL_AUTH_KEY_ACCESS_EXP, &out->access_expires_at_ms);
    }
    if (err == ESP_OK) {
        err = nvs_get_i64(nvs, OFFICIAL_AUTH_KEY_REFRESH_EXP, &out->refresh_expires_at_ms);
    }
    nvs_close(nvs);

    if (err != ESP_OK ||
        out->access_expires_at_ms <= 0 ||
        out->refresh_expires_at_ms <= 0) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t official_auth_save(const netease_token_set_t *tokens)
{
    if (tokens == NULL ||
        !token_text_present(tokens->access_token) ||
        !token_text_present(tokens->refresh_token) ||
        tokens->access_expires_at_ms <= 0 ||
        tokens->refresh_expires_at_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OFFICIAL_AUTH_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, OFFICIAL_AUTH_KEY_ACCESS, tokens->access_token);
    if (err == ESP_OK) err = nvs_set_str(nvs, OFFICIAL_AUTH_KEY_REFRESH, tokens->refresh_token);
    if (err == ESP_OK) err = nvs_set_i64(nvs, OFFICIAL_AUTH_KEY_ACCESS_EXP, tokens->access_expires_at_ms);
    if (err == ESP_OK) err = nvs_set_i64(nvs, OFFICIAL_AUTH_KEY_REFRESH_EXP, tokens->refresh_expires_at_ms);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t official_auth_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OFFICIAL_AUTH_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(nvs);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
#endif
