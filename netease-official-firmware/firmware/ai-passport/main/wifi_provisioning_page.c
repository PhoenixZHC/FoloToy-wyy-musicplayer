#include "wifi_provisioning_page.h"

#include "device_music_ui.h"
#include "demo_radio.h"
#include "wifi_provisioning_logic.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define PROV_NAMESPACE "official_wifi"
#define PROV_AP_PREFIX "AI-Passport"
#define PROV_FORM_MAX 512
#define PROV_SSID_LEN 33
#define PROV_PASSWORD_LEN 65

typedef struct {
    char ssid[PROV_SSID_LEN];
    char password[PROV_PASSWORD_LEN];
} provisioning_config_t;

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
static provisioning_config_t s_pending_config;
static char s_ap_ssid[32];
static char s_flow_detail[160];
static char s_rendered_detail[160];
static char s_rendered_ap_ssid[32];
static device_music_screen_t s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
static volatile bool s_wifi_ready;
static volatile bool s_wifi_started;
static volatile bool s_has_pending_config;
static volatile bool s_sta_got_ip;
static volatile bool s_exit_requested;
static volatile esp_err_t s_last_error;
static void (*s_complete_callback)(void);

static void copy_limited(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static esp_err_t nvs_read_str(nvs_handle_t nvs, const char *key, char *value, size_t value_len)
{
    size_t required = value_len;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    if (err != ESP_OK) value[0] = '\0';
    return err;
}

static bool load_config(provisioning_config_t *config)
{
    memset(config, 0, sizeof(*config));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;
    err = nvs_read_str(nvs, "ssid", config->ssid, sizeof(config->ssid));
    (void)nvs_read_str(nvs, "password", config->password, sizeof(config->password));
    nvs_close(nvs);
    return err == ESP_OK && wifi_provisioning_has_ready_config(config->ssid);
}

static esp_err_t save_config(const provisioning_config_t *config)
{
    if (config == NULL || !wifi_provisioning_has_ready_config(config->ssid)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, "ssid", config->ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", config->password);
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
        if (src[i] == '+') {
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
    const char *html =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AI Passport Wi-Fi</title></head><body>"
        "<h2>AI Passport Wi-Fi</h2>"
        "<form method='post' action='/save'>"
        "<p>Wi-Fi SSID<br><input name='ssid' maxlength='32' required></p>"
        "<p>Wi-Fi Password<br><input name='password' maxlength='64' type='password'></p>"
        "<button type='submit'>Save Wi-Fi</button></form></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
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

    provisioning_config_t form = {0};
    read_form_field(body, "ssid", form.ssid, sizeof(form.ssid));
    read_form_field(body, "password", form.password, sizeof(form.password));
    if (!wifi_provisioning_can_submit(form.ssid)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing Wi-Fi SSID");
        return ESP_FAIL;
    }
    s_pending_config = form;
    s_has_pending_config = true;
    if (s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
    return httpd_resp_sendstr(req, "Saved. Device is connecting to Wi-Fi now.");
}

static esp_err_t start_http_server(void)
{
    if (s_httpd != NULL) return ESP_OK;
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
    err = httpd_register_uri_handler(s_httpd, &root);
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
    if (s_wifi_started) return ESP_OK;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_ap_netif == NULL || s_sta_netif == NULL) return ESP_ERR_NO_MEM;

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
    if (!wifi_provisioning_has_ready_config(ssid)) return ESP_ERR_INVALID_ARG;
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
        if (s_sta_got_ip) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static void stop_wifi(void)
{
    stop_http_server();
    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
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
    if (s_wifi_ready) {
        esp_wifi_deinit();
        s_wifi_ready = false;
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

static esp_err_t connect_and_save(const provisioning_config_t *form)
{
    copy_limited(s_flow_detail, sizeof(s_flow_detail), "Connecting Wi-Fi...");
    esp_err_t err = connect_station(form->ssid, form->password);
    if (err != ESP_OK) {
        copy_limited(s_flow_detail, sizeof(s_flow_detail), "Wi-Fi connect failed\nPress OK to retry");
        return err;
    }
    err = save_config(form);
    if (err != ESP_OK) {
        copy_limited(s_flow_detail, sizeof(s_flow_detail), "Wi-Fi save failed");
        return err;
    }
    s_config = *form;
    copy_limited(s_flow_detail, sizeof(s_flow_detail), "Wi-Fi connected\nPress OK to login");
    return ESP_OK;
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_exit_requested) break;
        if (!s_wifi_started) {
            s_last_error = ensure_wifi_started();
            if (s_last_error != ESP_OK) {
                wifi_provisioning_logic_on_connect_result(&s_logic, false);
                continue;
            }
        }
        if (s_has_pending_config) {
            provisioning_config_t form = s_pending_config;
            s_has_pending_config = false;
            s_logic.has_saved_config = true;
            s_logic.state = WIFI_PROVISIONING_CONNECTING;
            s_last_error = connect_and_save(&form);
            wifi_provisioning_logic_on_connect_result(&s_logic, s_last_error == ESP_OK);
        } else if (s_logic.state == WIFI_PROVISIONING_CONNECTING && s_config.ssid[0] != '\0') {
            s_last_error = connect_station(s_config.ssid, s_config.password);
            wifi_provisioning_logic_on_connect_result(&s_logic, s_last_error == ESP_OK);
            if (s_last_error == ESP_OK) {
                copy_limited(s_flow_detail, sizeof(s_flow_detail), "Wi-Fi connected\nPress OK to login");
            }
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

static bool provisioning_needs_rerender(device_music_screen_t screen_id)
{
    return s_scr == NULL ||
           s_rendered_screen != screen_id ||
           strcmp(s_rendered_detail, s_flow_detail) != 0 ||
           strcmp(s_rendered_ap_ssid, s_ap_ssid) != 0;
}

static void render_provisioning_ui(void)
{
    device_music_screen_t screen_id = provisioning_screen_for_state();
    if (!provisioning_needs_rerender(screen_id)) return;
    lv_obj_t *old = s_scr;
    lv_obj_t *screen = device_music_ui_create_screen(screen_id);
    switch (screen_id) {
    case DEVICE_MUSIC_SCREEN_SETUP:
        device_music_ui_draw_setup(screen, s_ap_ssid);
        break;
    case DEVICE_MUSIC_SCREEN_REGISTERING:
        device_music_ui_draw_progress(screen, screen_id, "Connecting Wi-Fi", s_flow_detail, 2);
        break;
    case DEVICE_MUSIC_SCREEN_READY:
        device_music_ui_draw_success(screen, "Wi-Fi Ready", s_flow_detail);
        break;
    case DEVICE_MUSIC_SCREEN_SETUP_FAILED:
        device_music_ui_draw_error(screen, screen_id, "Wi-Fi Failed", s_flow_detail);
        break;
    default:
        device_music_ui_draw_message(screen, screen_id, NULL, s_flow_detail);
        break;
    }
    s_scr = screen;
    lv_screen_load(s_scr);
    if (old != NULL) lv_obj_delete(old);
    s_rendered_screen = screen_id;
    copy_limited(s_rendered_detail, sizeof(s_rendered_detail), s_flow_detail);
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
    s_flow_detail[0] = '\0';
    s_rendered_detail[0] = '\0';
    s_rendered_ap_ssid[0] = '\0';
    s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
    s_exit_requested = false;
    render_provisioning_ui();
    s_timer = lv_timer_create(tick, 250, NULL);
    if (s_worker_task == NULL) {
        xTaskCreate(worker_task, "wifi_setup", 4096, NULL, 4, &s_worker_task);
    }
    if (s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
}

bool wifi_provisioning_page_has_saved_config(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;
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
    if (s_worker_task == NULL) stop_wifi();
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
}

void wifi_provisioning_page_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK) return;
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
    render_provisioning_ui();
}
