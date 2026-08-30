#include "netease_official_client.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "official_http";

typedef struct {
    char *response;
    size_t response_size;
    size_t len;
    bool overflow;
} official_response_capture_t;

static esp_err_t official_http_event_handler(esp_http_client_event_t *event)
{
    if (event == NULL || event->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }

    official_response_capture_t *capture = (official_response_capture_t *)event->user_data;
    if (capture == NULL || capture->response == NULL || capture->response_size == 0 || event->data == NULL) {
        return ESP_OK;
    }

    if (capture->len + (size_t)event->data_len >= capture->response_size) {
        capture->overflow = true;
        return ESP_OK;
    }

    memcpy(capture->response + capture->len, event->data, (size_t)event->data_len);
    capture->len += (size_t)event->data_len;
    capture->response[capture->len] = '\0';
    return ESP_OK;
}
#endif

static void copy_json_text(cJSON *object, const char *key, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) return;
    out[0] = '\0';
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(out, out_size, "%s", item->valuestring);
    }
}

void official_api_result_clear(official_api_result_t *result)
{
    if (result == NULL) return;
    if (result->root != NULL) {
        cJSON_Delete(result->root);
    }
    memset(result, 0, sizeof(*result));
}

esp_err_t official_parse_envelope(char *response,
                                  int http_status,
                                  official_api_result_t *out)
{
    if (response == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->http_status = http_status;

    size_t len = strlen(response);
    if (len == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (len > NETEASE_OFFICIAL_RESPONSE_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_Parse(response);
    if (!cJSON_IsObject(root)) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Invalid JSON response http=%d len=%u prefix=%.120s", http_status, (unsigned)len, response);
#endif
        if (root != NULL) cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->root = root;

    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code) || code->valuedouble != (double)code->valueint) {
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG, "Invalid envelope http=%d len=%u prefix=%.120s", http_status, (unsigned)len, response);
#endif
        official_api_result_clear(out);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->code = code->valueint;
    copy_json_text(root, "subCode", out->sub_code, sizeof(out->sub_code));
    copy_json_text(root, "message", out->message, sizeof(out->message));

    if (http_status < 200 || http_status >= 300) {
        out->data = NULL;
        return ESP_FAIL;
    }

    if (out->code != 200) {
        out->data = NULL;
#ifdef ESP_PLATFORM
        ESP_LOGW(TAG,
                 "Business error http=%d code=%d subCode=%s message=%s",
                 http_status,
                 out->code,
                 out->sub_code,
                 out->message);
#endif
        return ESP_FAIL;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (data == NULL) {
        out->data = NULL;
        return ESP_OK;
    }
    if (!cJSON_IsObject(data)) {
        official_api_result_clear(out);
        return ESP_ERR_INVALID_RESPONSE;
    }
    out->data = data;
    return ESP_OK;
}

esp_err_t official_http_request(const netease_official_client_t *client,
                                const char *path,
                                esp_http_client_method_t method,
                                const char *encoded_query,
                                char *response,
                                size_t response_size,
                                int *http_status)
{
    if (client == NULL || path == NULL || response == NULL || response_size == 0 || http_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (path[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    *http_status = 0;

#ifndef ESP_PLATFORM
    (void)method;
    (void)encoded_query;
    return ESP_ERR_INVALID_STATE;
#else
    char url[256] = {0};
    const char *query = encoded_query == NULL ? "" : encoded_query;
    if (method == HTTP_METHOD_GET && query[0] != '\0') {
        int written = snprintf(url, sizeof(url), "%s%s?%s", NETEASE_OFFICIAL_HOST, path, query);
        if (written < 0 || (size_t)written >= sizeof(url)) {
            return ESP_ERR_INVALID_SIZE;
        }
    } else {
        int written = snprintf(url, sizeof(url), "%s%s", NETEASE_OFFICIAL_HOST, path);
        if (written < 0 || (size_t)written >= sizeof(url)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    official_response_capture_t capture = {
        .response = response,
        .response_size = response_size,
        .len = 0,
        .overflow = false,
    };

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = client->config.timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
        .event_handler = official_http_event_handler,
        .user_data = &capture,
    };
    esp_http_client_handle_t handle = esp_http_client_init(&http_config);
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_method(handle, method);
    if (err == ESP_OK && method == HTTP_METHOD_POST) {
        err = esp_http_client_set_header(handle, "Content-Type", "application/x-www-form-urlencoded");
    }
    if (err == ESP_OK && method == HTTP_METHOD_POST && query[0] != '\0') {
        err = esp_http_client_set_post_field(handle, query, (int)strlen(query));
    }
    if (err == ESP_OK) {
        err = esp_http_client_perform(handle);
    }
    if (err == ESP_OK) {
        *http_status = esp_http_client_get_status_code(handle);
        int content_length = esp_http_client_get_content_length(handle);
        if (content_length >= 0 && (size_t)content_length >= response_size) {
            err = ESP_ERR_INVALID_SIZE;
        } else if (capture.overflow) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            response[capture.len] = '\0';
            ESP_LOGI(TAG,
                     "HTTP %s status=%d content_length=%d read=%d",
                     path,
                     *http_status,
                     content_length,
                     (int)capture.len);
        }
    }

    esp_http_client_cleanup(handle);
    if (err != ESP_OK) {
        response[0] = '\0';
    }
    return err;
#endif
}
