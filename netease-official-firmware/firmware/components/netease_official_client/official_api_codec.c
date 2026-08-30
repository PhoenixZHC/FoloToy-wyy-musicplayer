#include "netease_official_client.h"

#include <stdio.h>
#include <string.h>

static bool is_unreserved(unsigned char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static esp_err_t append_char(char *output, size_t output_size, size_t *len, char ch)
{
    if (*len + 1 >= output_size) {
        if (output_size > 0) output[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    output[*len] = ch;
    *len += 1;
    output[*len] = '\0';
    return ESP_OK;
}

static esp_err_t append_text(char *output, size_t output_size, size_t *len, const char *text)
{
    while (*text != '\0') {
        esp_err_t err = append_char(output, output_size, len, *text);
        if (err != ESP_OK) return err;
        text++;
    }
    return ESP_OK;
}

esp_err_t official_url_encode(const char *input, char *output, size_t output_size)
{
    if (input == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    static const char hex[] = "0123456789ABCDEF";
    size_t len = 0;
    output[0] = '\0';
    for (const unsigned char *p = (const unsigned char *)input; *p != '\0'; p++) {
        if (is_unreserved(*p)) {
            esp_err_t err = append_char(output, output_size, &len, (char)*p);
            if (err != ESP_OK) return err;
        } else {
            if (len + 3 >= output_size) {
                output[0] = '\0';
                return ESP_ERR_INVALID_SIZE;
            }
            output[len++] = '%';
            output[len++] = hex[(*p >> 4) & 0x0f];
            output[len++] = hex[*p & 0x0f];
            output[len] = '\0';
        }
    }
    return ESP_OK;
}

static bool param_is_sign_or_empty(const official_param_t *param)
{
    return param->key == NULL ||
           param->value == NULL ||
           param->value[0] == '\0' ||
           strcmp(param->key, "sign") == 0;
}

esp_err_t official_build_canonical(const official_param_t *params,
                                   size_t count,
                                   char *output,
                                   size_t output_size)
{
    if ((params == NULL && count > 0) || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const official_param_t *ordered[16] = {0};
    size_t ordered_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (param_is_sign_or_empty(&params[i])) {
            continue;
        }
        if (ordered_count >= sizeof(ordered) / sizeof(ordered[0])) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t pos = ordered_count;
        while (pos > 0 && strcmp(ordered[pos - 1]->key, params[i].key) > 0) {
            ordered[pos] = ordered[pos - 1];
            pos--;
        }
        ordered[pos] = &params[i];
        ordered_count++;
    }

    size_t len = 0;
    output[0] = '\0';
    for (size_t i = 0; i < ordered_count; i++) {
        if (i > 0) {
            esp_err_t err = append_char(output, output_size, &len, '&');
            if (err != ESP_OK) return err;
        }
        esp_err_t err = append_text(output, output_size, &len, ordered[i]->key);
        if (err != ESP_OK) return err;
        err = append_char(output, output_size, &len, '=');
        if (err != ESP_OK) return err;
        err = append_text(output, output_size, &len, ordered[i]->value);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t append_json_string(char *output, size_t output_size, size_t *len, const char *text)
{
    esp_err_t err = append_char(output, output_size, len, '"');
    if (err != ESP_OK) return err;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            err = append_char(output, output_size, len, '\\');
            if (err != ESP_OK) return err;
        }
        err = append_char(output, output_size, len, *p);
        if (err != ESP_OK) return err;
    }
    return append_char(output, output_size, len, '"');
}

static esp_err_t append_json_pair(char *output,
                                  size_t output_size,
                                  size_t *len,
                                  const char *key,
                                  const char *value,
                                  bool comma)
{
    esp_err_t err = ESP_OK;
    if (comma) {
        err = append_char(output, output_size, len, ',');
        if (err != ESP_OK) return err;
    }
    err = append_json_string(output, output_size, len, key);
    if (err != ESP_OK) return err;
    err = append_char(output, output_size, len, ':');
    if (err != ESP_OK) return err;
    return append_json_string(output, output_size, len, value);
}

esp_err_t official_build_device_json(const char *device_id,
                                     const char *app_version,
                                     char *output,
                                     size_t output_size)
{
    if (device_id == NULL || app_version == NULL || output == NULL || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = 0;
    output[0] = '\0';
    esp_err_t err = append_char(output, output_size, &len, '{');
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "netStatus", NETEASE_OFFICIAL_NET_STATUS, false);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "channel", NETEASE_OFFICIAL_CHANNEL, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "deviceId", device_id, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "deviceType", NETEASE_OFFICIAL_DEVICE_TYPE, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "appVer", app_version, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "os", NETEASE_OFFICIAL_OS, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "osVer", NETEASE_OFFICIAL_OS_VERSION, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "brand", NETEASE_OFFICIAL_BRAND, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "model", NETEASE_OFFICIAL_MODEL, true);
    if (err == ESP_OK) err = append_json_pair(output, output_size, &len, "clientIp", NETEASE_OFFICIAL_CLIENT_IP, true);
    if (err == ESP_OK) err = append_char(output, output_size, &len, '}');
    if (err != ESP_OK && output_size > 0) output[0] = '\0';
    return err;
}
