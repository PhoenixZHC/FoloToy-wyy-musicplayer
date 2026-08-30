#include "wifi_provisioning_page.h"

#include "device_music_ui.h"
#include "demo_radio.h"
#include "netease_client.h"
#include "wifi_provisioning_logic.h"
#include "wifi_provisioning_portal.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROV_NAMESPACE "cloud"
#define PROV_AP_PREFIX "AI-Passport"
#define PROV_FORM_MAX 1024
#define PROV_SSID_LEN 33
#define PROV_PASSWORD_LEN 65
#define PROV_REG_CODE_LEN 96
#define PROV_SCAN_RAW_MAX 32
#define PROV_SCAN_JSON_MAX 2048

typedef struct {
    char ssid[PROV_SSID_LEN];
    char password[PROV_PASSWORD_LEN];
    char cloud_url[NETEASE_CLIENT_BASE_URL_LEN];
    char device_id[NETEASE_CLIENT_DEVICE_ID_LEN];
    char device_secret[NETEASE_CLIENT_DEVICE_SECRET_LEN];
    bool registered;
} provisioning_config_t;

typedef struct {
    char ssid[PROV_SSID_LEN];
    char password[PROV_PASSWORD_LEN];
    char cloud_url[NETEASE_CLIENT_BASE_URL_LEN];
    char registration_code[PROV_REG_CODE_LEN];
} provisioning_form_t;

static const char *TAG = "wifi_provisioning";
static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_any_handler;
static esp_event_handler_instance_t s_ip_handler;
static TaskHandle_t s_worker_task;
static wifi_provisioning_logic_t s_logic;
static provisioning_config_t s_config;
static provisioning_form_t s_pending_form;
static char s_ap_ssid[32];
static volatile bool s_wifi_ready;
static volatile bool s_wifi_started;
static volatile bool s_has_pending_form;
static volatile bool s_sta_got_ip;
static volatile bool s_exit_requested;
static volatile esp_err_t s_last_error;
static char s_flow_detail[192];
static char s_rendered_detail[192];
static char s_rendered_ap_ssid[32];
static device_music_screen_t s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
static void (*s_complete_callback)(void);

static esp_err_t nvs_read_str(nvs_handle_t nvs, const char *key, char *value, size_t value_len)
{
    size_t required = value_len;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    if (err != ESP_OK) {
        value[0] = '\0';
    }
    return err;
}

static void copy_limited(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static bool load_config(provisioning_config_t *config)
{
    memset(config, 0, sizeof(*config));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_read_str(nvs, "ssid", config->ssid, sizeof(config->ssid));
    if (err == ESP_OK) err = nvs_read_str(nvs, "cloud_url", config->cloud_url, sizeof(config->cloud_url));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_id", config->device_id, sizeof(config->device_id));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_secret", config->device_secret, sizeof(config->device_secret));
    uint8_t registered = 0;
    if (err == ESP_OK) {
        (void)nvs_get_u8(nvs, "registered", &registered);
        config->registered = registered == 1;
    }
    (void)nvs_read_str(nvs, "password", config->password, sizeof(config->password));
    nvs_close(nvs);

    return err == ESP_OK &&
           wifi_provisioning_has_ready_config(config->cloud_url,
                                              config->device_id,
                                              config->device_secret,
                                              config->registered);
}

static esp_err_t save_config(const provisioning_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    if (err == ESP_OK) err = nvs_set_str(nvs, "ssid", config->ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", config->password);
    if (err == ESP_OK) err = nvs_set_str(nvs, "cloud_url", config->cloud_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_id", config->device_id);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_secret", config->device_secret);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "registered", config->registered ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static void clear_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open(PROV_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    memset(&s_config, 0, sizeof(s_config));
}

static void generate_identity(provisioning_config_t *config)
{
    if (config->device_id[0] == '\0') {
        snprintf(config->device_id, sizeof(config->device_id),
                 "dev_%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    }
    if (config->device_secret[0] == '\0') {
        snprintf(config->device_secret, sizeof(config->device_secret),
                 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
                 esp_random(), esp_random(), esp_random(), esp_random(),
                 esp_random(), esp_random(), esp_random(), esp_random());
    }
}

static void set_flow_detail(const char *text)
{
    copy_limited(s_flow_detail, sizeof(s_flow_detail), text);
}

static esp_err_t run_netease_smoke_flow(const netease_client_t *client)
{
    set_flow_detail("Device registered\nChecking relay flow...");

    esp_err_t err = netease_client_hello(client);
    if (err != ESP_OK) {
        set_flow_detail("Device registered\nHello failed");
        return err;
    }

    netease_qr_start_t qr = {0};
    err = netease_client_qr_start(client, &qr);
    if (err != ESP_OK) {
        set_flow_detail("Device registered\nQR start failed");
        return err;
    }

    netease_qr_status_t qr_status = {0};
    err = netease_client_qr_status(client, qr.login_id, &qr_status);
    if (err != ESP_OK) {
        set_flow_detail("Device registered\nQR status failed");
        return err;
    }
    if (qr_status.state != NETEASE_QR_CONFIRMED) {
        set_flow_detail("Device registered\nQR not confirmed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    netease_playlist_list_t *playlists = calloc(1, sizeof(*playlists));
    netease_track_list_t *tracks = calloc(1, sizeof(*tracks));
    if (playlists == NULL || tracks == NULL) {
        free(playlists);
        free(tracks);
        set_flow_detail("Device registered\nNot enough memory");
        return ESP_ERR_NO_MEM;
    }

    err = netease_client_get_playlists(client, playlists);
    if (err == ESP_OK && playlists->count == 0) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        err = netease_client_get_tracks(client, playlists->items[0].id, 10, 0, tracks);
    }
    if (err == ESP_OK && tracks->count == 0) {
        err = ESP_ERR_NOT_FOUND;
    }

    netease_track_url_t track_url = {0};
    if (err == ESP_OK) {
        err = netease_client_get_track_url(client, tracks->items[0].id, 128000, &track_url);
    }

    if (err == ESP_OK) {
        snprintf(s_flow_detail,
                 sizeof(s_flow_detail),
                 "Relay flow OK\nPlaylists: %u\nTracks: %u\nTrack URL OK",
                 (unsigned)playlists->count,
                 (unsigned)tracks->count);
    } else {
        snprintf(s_flow_detail, sizeof(s_flow_detail), "Relay flow failed\n%s", esp_err_to_name(err));
    }

    free(playlists);
    free(tracks);
    return err;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dst_len; i++) {
        if (src[i] == '+' ) {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static void read_form_field(const char *body, const char *key, char *dst, size_t dst_len)
{
    dst[0] = '\0';
    size_t key_len = strlen(key);
    const char *cursor = body;
    while (cursor != NULL && *cursor != '\0') {
        const char *next = strchr(cursor, '&');
        size_t pair_len = next ? (size_t)(next - cursor) : strlen(cursor);
        const char *eq = memchr(cursor, '=', pair_len);
        if (eq != NULL && (size_t)(eq - cursor) == key_len && strncmp(cursor, key, key_len) == 0) {
            url_decode(dst, dst_len, eq + 1, pair_len - key_len - 1);
            return;
        }
        cursor = next ? next + 1 : NULL;
    }
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, wifi_provisioning_portal_html(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed to start: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "scan unavailable", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t found = 0;
    err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan count failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "scan failed", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t raw_count = found > PROV_SCAN_RAW_MAX ? PROV_SCAN_RAW_MAX : found;
    wifi_ap_record_t *records = raw_count > 0 ? calloc(raw_count, sizeof(*records)) : NULL;
    wifi_provisioning_scan_entry_t *raw_entries =
        raw_count > 0 ? calloc(raw_count, sizeof(*raw_entries)) : NULL;
    if (raw_count > 0 && (records == NULL || raw_entries == NULL)) {
        free(records);
        free(raw_entries);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "out of memory", HTTPD_RESP_USE_STRLEN);
    }

    if (raw_count > 0) {
        err = esp_wifi_scan_get_ap_records(&raw_count, records);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi scan results failed: %s", esp_err_to_name(err));
            free(records);
            free(raw_entries);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "scan failed", HTTPD_RESP_USE_STRLEN);
        }
        for (uint16_t index = 0; index < raw_count; index++) {
            snprintf(raw_entries[index].ssid,
                     sizeof(raw_entries[index].ssid),
                     "%s",
                     (const char *)records[index].ssid);
            raw_entries[index].rssi = records[index].rssi;
            raw_entries[index].secure = records[index].authmode != WIFI_AUTH_OPEN;
        }
    }

    wifi_provisioning_scan_entry_t visible[WIFI_PROVISIONING_SCAN_MAX_RESULTS] = {0};
    size_t visible_count = wifi_provisioning_prepare_scan_results(
        raw_entries,
        raw_count,
        visible,
        WIFI_PROVISIONING_SCAN_MAX_RESULTS);
    free(records);
    free(raw_entries);

    char *json = calloc(PROV_SCAN_JSON_MAX, sizeof(*json));
    if (json == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "out of memory", HTTPD_RESP_USE_STRLEN);
    }
    if (wifi_provisioning_scan_results_json(visible, visible_count, json, PROV_SCAN_JSON_MAX) == 0) {
        free(json);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "scan response too large", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= PROV_FORM_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }

    char body[PROV_FORM_MAX] = {0};
    int received = 0;
    while (received < req->content_len) {
        int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "read failed");
            return ESP_FAIL;
        }
        received += chunk;
    }
    body[received] = '\0';

    provisioning_form_t form = {0};
    read_form_field(body, "ssid", form.ssid, sizeof(form.ssid));
    read_form_field(body, "password", form.password, sizeof(form.password));
    read_form_field(body, "cloud_url", form.cloud_url, sizeof(form.cloud_url));
    read_form_field(body, "registration_code", form.registration_code, sizeof(form.registration_code));

    if (!wifi_provisioning_can_submit(form.ssid, form.cloud_url, form.registration_code)) {
        httpd_resp_send_err(req,
                            HTTPD_400_BAD_REQUEST,
                            "invalid setup data; use the computer Wi-Fi IP for Relay URL, not 192.168.4.x");
        return ESP_FAIL;
    }

    s_pending_form = form;
    s_has_pending_form = true;
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, wifi_provisioning_success_html(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_http_server(void)
{
    if (s_httpd != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        s_httpd = NULL;
        return err;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
    };
    httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };
    err = httpd_register_uri_handler(s_httpd, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_httpd, &scan);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_httpd, &save);
    return err;
}

static void stop_http_server(void)
{
    if (s_httpd != NULL) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_got_ip = false;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        s_sta_got_ip = true;
    }
}

static esp_err_t ensure_wifi_started(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_ap_netif == NULL || s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    s_wifi_ready = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL, &s_wifi_any_handler);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              ip_event_handler, NULL, &s_ip_handler);
    if (err != ESP_OK) return err;

    uint32_t suffix = esp_random() & 0xffff;
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%04" PRIX32, PROV_AP_PREFIX, suffix);

    wifi_config_t ap_config = {0};
    copy_limited((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), s_ap_ssid);
    ap_config.ap.ssid_len = strlen(s_ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    s_wifi_started = true;
    return start_http_server();
}

static esp_err_t connect_station(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_ap_record_t current_ap = {0};
    esp_err_t ap_info_err = esp_wifi_sta_get_ap_info(&current_ap);
    if (ap_info_err != ESP_OK && ap_info_err != ESP_ERR_WIFI_NOT_CONNECT) {
        return ap_info_err;
    }

    wifi_provisioning_station_action_t station_action =
        wifi_provisioning_station_action(ap_info_err == ESP_OK,
                                         s_sta_got_ip,
                                         (const char *)current_ap.ssid,
                                         ssid);
    if (station_action == WIFI_PROVISIONING_STA_REUSE) {
        ESP_LOGI(TAG, "Reusing existing Wi-Fi connection: %s", ssid);
        return ESP_OK;
    }
    if (station_action == WIFI_PROVISIONING_STA_RECONNECT) {
        ESP_LOGI(TAG, "Disconnecting current Wi-Fi before reconnecting: %s", ssid);
        s_sta_got_ip = false;
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) {
            return err;
        }
    }

    wifi_config_t sta_config = {0};
    copy_limited((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), ssid);
    copy_limited((char *)sta_config.sta.password, sizeof(sta_config.sta.password), password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    s_sta_got_ip = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) return err;
    err = esp_wifi_connect();
    if (err != ESP_OK) return err;

    for (int i = 0; i < 200; i++) {
        if (s_sta_got_ip) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static void stop_wifi(void)
{
    stop_http_server();
    if (s_wifi_ready) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_ready = false;
        s_wifi_started = false;
    }
    if (s_wifi_any_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_any_handler);
        s_wifi_any_handler = NULL;
    }
    if (s_ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
}

static esp_err_t register_device(const provisioning_form_t *form)
{
    provisioning_config_t next = s_config;
    set_flow_detail("");
    copy_limited(next.ssid, sizeof(next.ssid), form->ssid);
    copy_limited(next.password, sizeof(next.password), form->password);
    copy_limited(next.cloud_url, sizeof(next.cloud_url), form->cloud_url);
    generate_identity(&next);
    next.registered = false;
    esp_err_t err = save_config(&next);
    if (err != ESP_OK) return err;

    if (next.ssid[0] != '\0') {
        err = connect_station(next.ssid, next.password);
        if (err != ESP_OK) return err;
    }

    netease_client_t client = {0};
    netease_client_config_t client_config = {
        .base_url = next.cloud_url,
        .device_id = next.device_id,
        .device_secret = next.device_secret,
        .timeout_ms = 10000,
    };
    err = netease_client_init(&client, &client_config);
    if (err != ESP_OK) return err;
    err = netease_client_register_device(&client, "AI-Passport", form->registration_code);
    if (err != ESP_OK) return err;

    next.registered = true;
    err = save_config(&next);
    if (err == ESP_OK) {
        s_config = next;
    }
    if (err == ESP_OK && wifi_provisioning_should_run_mock_smoke(next.ssid)) {
        err = run_netease_smoke_flow(&client);
    } else if (err == ESP_OK) {
        set_flow_detail("Device registered\nPress OK to continue");
    }
    return err;
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_exit_requested) {
            break;
        }
        if (!s_wifi_started) {
            s_last_error = ensure_wifi_started();
            if (s_last_error != ESP_OK) {
                s_logic.state = WIFI_PROVISIONING_FAILED;
                continue;
            }
        }
        if (s_has_pending_form) {
            provisioning_form_t form = s_pending_form;
            s_has_pending_form = false;
            s_logic.has_saved_config = true;
            s_logic.state = WIFI_PROVISIONING_CONNECTING;
            s_last_error = register_device(&form);
            wifi_provisioning_logic_on_connect_result(&s_logic, s_last_error == ESP_OK);
        } else if (s_logic.state == WIFI_PROVISIONING_CONNECTING && s_config.ssid[0] != '\0') {
            s_last_error = connect_station(s_config.ssid, s_config.password);
            wifi_provisioning_logic_on_connect_result(&s_logic, s_last_error == ESP_OK);
        }
    }
    stop_wifi();
    s_worker_task = NULL;
    s_exit_requested = false;
    vTaskDelete(NULL);
}

static device_music_screen_t provisioning_screen_for_state(void)
{
    switch (wifi_provisioning_logic_state(&s_logic)) {
    case WIFI_PROVISIONING_NEEDS_SETUP:
        return DEVICE_MUSIC_SCREEN_SETUP;
    case WIFI_PROVISIONING_CONNECTING:
        return DEVICE_MUSIC_SCREEN_REGISTERING;
    case WIFI_PROVISIONING_CONNECTED:
        return DEVICE_MUSIC_SCREEN_READY;
    case WIFI_PROVISIONING_FAILED:
    default:
        return DEVICE_MUSIC_SCREEN_SETUP_FAILED;
    }
}

static const char *provisioning_detail(void)
{
    if (wifi_provisioning_logic_state(&s_logic) == WIFI_PROVISIONING_CONNECTING && s_flow_detail[0]) {
        return s_flow_detail;
    }
    if (wifi_provisioning_logic_state(&s_logic) == WIFI_PROVISIONING_CONNECTED && s_flow_detail[0]) {
        return s_flow_detail;
    }
    return "";
}

static bool provisioning_needs_rerender(device_music_screen_t screen_id)
{
    return s_scr == NULL ||
           s_rendered_screen != screen_id ||
           strcmp(s_rendered_detail, provisioning_detail()) != 0 ||
           strcmp(s_rendered_ap_ssid, s_ap_ssid) != 0;
}

static void render_provisioning_ui(void)
{
    device_music_screen_t screen_id = provisioning_screen_for_state();
    if (!provisioning_needs_rerender(screen_id)) {
        return;
    }

    lv_obj_t *old = s_scr;
    lv_obj_t *screen = device_music_ui_create_screen(screen_id);
    switch (screen_id) {
    case DEVICE_MUSIC_SCREEN_SETUP:
        device_music_ui_draw_setup(screen, s_ap_ssid);
        break;
    case DEVICE_MUSIC_SCREEN_REGISTERING:
        device_music_ui_draw_progress(screen, screen_id, NULL, NULL, s_flow_detail[0] ? 3 : 2);
        break;
    case DEVICE_MUSIC_SCREEN_READY:
        device_music_ui_draw_success(screen, NULL, NULL);
        break;
    case DEVICE_MUSIC_SCREEN_SETUP_FAILED:
        device_music_ui_draw_error(screen, screen_id, NULL, NULL);
        break;
    default:
        device_music_ui_draw_message(screen, screen_id, NULL, NULL);
        break;
    }

    s_scr = screen;
    lv_screen_load(s_scr);
    if (old != NULL) {
        lv_obj_delete(old);
    }
    s_rendered_screen = screen_id;
    copy_limited(s_rendered_detail, sizeof(s_rendered_detail), provisioning_detail());
    copy_limited(s_rendered_ap_ssid, sizeof(s_rendered_ap_ssid), s_ap_ssid);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    render_provisioning_ui();
}

void wifi_provisioning_page_enter(void)
{
    bool has_config = load_config(&s_config);
    wifi_provisioning_logic_init(&s_logic, has_config);
    s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
    s_rendered_detail[0] = '\0';
    s_rendered_ap_ssid[0] = '\0';
    s_exit_requested = false;
    render_provisioning_ui();
    s_timer = lv_timer_create(tick, 250, NULL);
    if (s_worker_task == NULL) {
        xTaskCreate(worker_task, "cloud_setup", 6144, NULL, 4, &s_worker_task);
    }
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

bool wifi_provisioning_page_has_saved_config(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) {
        return false;
    }
    provisioning_config_t config = {0};
    return load_config(&config);
}

void wifi_provisioning_page_set_complete_callback(void (*callback)(void))
{
    s_complete_callback = callback;
}

void wifi_provisioning_page_exit(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_worker_task) {
        s_exit_requested = true;
        xTaskNotifyGive(s_worker_task);
        for (int i = 0; i < 100 && s_worker_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (s_worker_task == NULL) {
        stop_wifi();
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
}

void wifi_provisioning_page_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK) {
        return;
    }
    if (ev == BSP_BTN_LONG) {
        clear_config();
        wifi_provisioning_logic_on_ok_long(&s_logic);
    } else if (ev == BSP_BTN_CLICK) {
        if (s_logic.state == WIFI_PROVISIONING_CONNECTED &&
            s_complete_callback != NULL &&
            wifi_provisioning_page_has_saved_config()) {
            s_complete_callback();
            return;
        }
        wifi_provisioning_logic_on_ok_click(&s_logic);
        if (s_logic.state == WIFI_PROVISIONING_CONNECTING && s_worker_task != NULL) {
            xTaskNotifyGive(s_worker_task);
        }
    }
}
