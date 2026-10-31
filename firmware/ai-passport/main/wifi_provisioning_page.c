#include "wifi_provisioning_page.h"
#include "wifi_provisioning_logic.h"
#include "device_music_ui.h"
#include "demo_radio.h"
#include "bsp_display.h"
#include "netease_client.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CONFIG_NETEASE_DIRECT
#define PROV_NAMESPACE "netease_wifi"
#else
#define PROV_NAMESPACE "cloud"
#endif
#define LOCAL_WIFI_MAX 24
#define SCAN_RAW_MAX 32
#define PROV_SSID_LEN 33
#define PROV_PASSWORD_LEN 65
typedef struct {
    char ssid[PROV_SSID_LEN];
    char password[PROV_PASSWORD_LEN];
    char cloud_url[NETEASE_CLIENT_BASE_URL_LEN];
    char device_id[NETEASE_CLIENT_DEVICE_ID_LEN];
    char device_secret[NETEASE_CLIENT_DEVICE_SECRET_LEN];
    bool registered;
} provisioning_config_t;
typedef struct { char ssid[33]; wifi_auth_mode_t auth; } local_ap_t;
typedef enum { PAGE_SCAN, PAGE_SCANNING, PAGE_PASSWORD, PAGE_ACTIONS,
               PAGE_CONNECTING, PAGE_FAILED, PAGE_READY } local_page_t;
typedef enum { JOB_NONE, JOB_SCAN, JOB_CONNECT } local_job_t;
static const char *TAG = "wifi_provisioning";
static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static TaskHandle_t s_worker_task;
static portMUX_TYPE s_job_lock = portMUX_INITIALIZER_UNLOCKED;
static local_job_t s_job;
static provisioning_config_t s_job_config, s_config, s_attempt;
static local_page_t s_page;
static local_ap_t s_aps[LOCAL_WIFI_MAX];
static size_t s_count, s_selected, s_action;
static wifi_password_editor_t s_editor;
static char s_detail[120];
static bool s_dirty, s_finish_requested;
static volatile bool s_exit_requested, s_worker_done, s_sta_got_ip;
static bool s_wifi_ready, s_wifi_started;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static void (*s_complete_callback)(void);
RTC_NOINIT_ATTR static uint32_t s_setup_requested;
#define SETUP_REQUEST_MAGIC 0x46575053U
void wifi_provisioning_page_request_setup(void) { s_setup_requested = SETUP_REQUEST_MAGIC; }
bool wifi_provisioning_page_setup_requested(void)
{ return esp_reset_reason() == ESP_RST_SW && s_setup_requested == SETUP_REQUEST_MAGIC; }
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
#ifndef CONFIG_NETEASE_DIRECT
    if (err == ESP_OK) err = nvs_read_str(nvs, "cloud_url", config->cloud_url, sizeof(config->cloud_url));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_id", config->device_id, sizeof(config->device_id));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_secret", config->device_secret, sizeof(config->device_secret));
    uint8_t registered = 0;
    if (err == ESP_OK) {
        (void)nvs_get_u8(nvs, "registered", &registered);
        config->registered = registered == 1;
    }
#endif
    (void)nvs_read_str(nvs, "password", config->password, sizeof(config->password));
    nvs_close(nvs);

#ifdef CONFIG_NETEASE_DIRECT
    return err == ESP_OK && config->ssid[0] != 0;
#else
    return err == ESP_OK &&
           wifi_provisioning_has_ready_config(config->cloud_url,
                                              config->device_id,
                                              config->device_secret,
                                              config->registered);
#endif
}

static esp_err_t save_config(const provisioning_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    if (err == ESP_OK) err = nvs_set_str(nvs, "ssid", config->ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", config->password);
#ifndef CONFIG_NETEASE_DIRECT
    if (err == ESP_OK) err = nvs_set_str(nvs, "cloud_url", config->cloud_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_id", config->device_id);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_secret", config->device_secret);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "registered", config->registered ? 1 : 0);
#endif
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}


static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_got_ip = false;
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Local Wi-Fi disconnected; reason=%u", event ? (unsigned)event->reason : 0);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_sta_got_ip = true;
        ESP_LOGI(TAG, "Local Wi-Fi received IP");
    }
}
static esp_err_t start_wifi(void)
{
    if (s_wifi_started) return ESP_OK;
    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    if (!s_wifi_ready) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) return err;
        s_wifi_ready = true;
    }
    if (!s_wifi_handler) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, &s_wifi_handler);
        if (err != ESP_OK) return err;
    }
    if (!s_ip_handler) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, &s_ip_handler);
        if (err != ESP_OK) return err;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) s_wifi_started = true;
    return err;
}
static void stop_wifi(void)
{
    if (s_wifi_handler) { esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler); s_wifi_handler = NULL; }
    if (s_ip_handler) { esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler); s_ip_handler = NULL; }
    if (s_wifi_ready) {
        if (s_wifi_started) { (void)esp_wifi_disconnect(); (void)esp_wifi_stop(); }
        (void)esp_wifi_deinit(); s_wifi_ready = s_wifi_started = false;
    }
    if (s_sta_netif) { esp_netif_destroy_default_wifi(s_sta_netif); s_sta_netif = NULL; }
    s_sta_got_ip = false;
}
static esp_err_t scan_networks(local_ap_t *aps, size_t *count)
{
    *count = 0;
    wifi_ap_record_t *raw = calloc(SCAN_RAW_MAX, sizeof(*raw));
    if (!raw) return ESP_ERR_NO_MEM;
    wifi_scan_config_t cfg = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err == ESP_OK) {
        uint16_t found = SCAN_RAW_MAX;
        err = esp_wifi_scan_get_ap_records(&found, raw);
        if (err == ESP_OK) {
            for (unsigned i = 0; i < found && *count < LOCAL_WIFI_MAX; ++i) {
                if (!raw[i].ssid[0]) continue;
                bool duplicate = false;
                for (size_t j = 0; j < *count; ++j)
                    if (aps[j].auth == raw[i].authmode && !strcmp(aps[j].ssid, (char *)raw[i].ssid)) duplicate = true;
                if (!duplicate) {
                    memcpy(aps[*count].ssid, raw[i].ssid, 32);
                    aps[*count].ssid[32] = 0;
                    aps[(*count)++].auth = raw[i].authmode;
                }
            }
        }
    }
    free(raw);
    return err;
}
static esp_err_t connect_network(const provisioning_config_t *config)
{
    (void)esp_wifi_disconnect();
    s_sta_got_ip = false;
    wifi_config_t wifi = {0};
    /* Preserve full 32-byte SSIDs and 64-digit hexadecimal PSKs. */
    memcpy(wifi.sta.ssid, config->ssid, strlen(config->ssid));
    memcpy(wifi.sta.password, config->password, strlen(config->password));
    wifi.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) return err;
    for (int i = 0; i < 200 && !s_exit_requested; ++i) {
        if (s_sta_got_ip) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return s_exit_requested ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
}
static bool publish_lock(void)
{
    while (!s_exit_requested) if (bsp_lvgl_lock(100)) {
        if (!s_exit_requested) return true;
        bsp_lvgl_unlock();
    }
    return false;
}
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_exit_requested) break;
        portENTER_CRITICAL(&s_job_lock);
        local_job_t job = s_job;
        provisioning_config_t config = s_job_config;
        s_job = JOB_NONE;
        portEXIT_CRITICAL(&s_job_lock);
        if (job == JOB_NONE) continue;
        ESP_LOGI(TAG, "Local Wi-Fi job started: %s", job == JOB_SCAN ? "scan" : "connect");
        esp_err_t err = start_wifi();
        local_ap_t found[LOCAL_WIFI_MAX] = {0}; size_t count = 0;
        if (err == ESP_OK && job == JOB_SCAN) err = scan_networks(found, &count);
        if (err == ESP_OK && job == JOB_CONNECT) {
            err = connect_network(&config);
#ifndef CONFIG_NETEASE_DIRECT
            if (err == ESP_OK && !wifi_provisioning_has_ready_config(config.cloud_url,
                    config.device_id, config.device_secret, config.registered)) err = ESP_ERR_NOT_SUPPORTED;
#endif
            if (err == ESP_OK && !s_exit_requested) err = save_config(&config);
        }
        ESP_LOGI(TAG, "Local Wi-Fi %s: %s; results=%u", job == JOB_SCAN ? "scan" : "connect", esp_err_to_name(err), (unsigned)count);
        if (publish_lock()) {
            if (job == JOB_SCAN) {
                memcpy(s_aps, found, sizeof(found)); s_count = count; s_selected = 0; s_page = PAGE_SCAN;
                copy_limited(s_detail, sizeof(s_detail), err != ESP_OK ? "扫描失败，长按 OK 重试" : count ? "仅支持 2.4 GHz 网络" : "未找到网络，长按 OK 重试");
            } else {
                s_page = err == ESP_OK ? PAGE_READY : PAGE_FAILED;
                if (err == ESP_OK) s_config = config;
                copy_limited(s_detail, sizeof(s_detail), err == ESP_OK ? "新网络已保存，登录状态保留" : err == ESP_ERR_TIMEOUT ? "连接超时，请检查密码与信号" : err == ESP_ERR_NOT_SUPPORTED ? "中转版缺少注册配置，请使用直连版" : "连接或保存失败，请重试");
            }
            s_dirty = true;
            bsp_lvgl_unlock();
        }
    }
    stop_wifi();
    /* No UI access or network ownership after this publication. */
    s_worker_done = true;
    vTaskDelete(NULL);
}
static void request_job(local_job_t job)
{
    if (s_exit_requested) return;
    if (!s_worker_task && xTaskCreate(worker, "wifi_local", 6144, NULL, 4, &s_worker_task) != pdPASS) {
        s_worker_task = NULL; s_page = PAGE_SCAN; s_dirty = true;
        copy_limited(s_detail, sizeof(s_detail), "内存不足，长按 OK 重试");
        return;
    }
    portENTER_CRITICAL(&s_job_lock);
    s_job = job;
    if (job == JOB_CONNECT) s_job_config = s_attempt;
    portEXIT_CRITICAL(&s_job_lock);
    s_page = job == JOB_SCAN ? PAGE_SCANNING : PAGE_CONNECTING;
    s_dirty = true;
    xTaskNotifyGive(s_worker_task);
}
static const char *const s_actions[] = {"连接此网络", "删除末尾字符", "显示 / 隐藏密码", "继续输入", "返回网络列表"};
static void render(void)
{
    if (!s_dirty) return;
    s_dirty = false;
    device_music_ui_set_network(s_sta_got_ip, 0);
    if (!s_scr) s_scr = device_music_ui_create_screen(DEVICE_MUSIC_SCREEN_SETUP);
    else device_music_ui_reset_screen(s_scr, DEVICE_MUSIC_SCREEN_SETUP);
    if (s_page == PAGE_SCAN) {
        const char *names[LOCAL_WIFI_MAX]; bool secure[LOCAL_WIFI_MAX];
        for (size_t i = 0; i < s_count; ++i) { names[i] = s_aps[i].ssid; secure[i] = s_aps[i].auth != WIFI_AUTH_OPEN; }
        device_music_ui_draw_wifi_scan(s_scr, names, secure, s_count, s_selected, s_detail);
    } else if (s_page == PAGE_PASSWORD) {
        device_music_ui_draw_wifi_password(s_scr, s_attempt.ssid, s_editor.text,
            wifi_password_editor_characters(&s_editor), s_editor.character,
            s_editor.visible, wifi_password_editor_group(&s_editor), s_detail);
    } else if (s_page == PAGE_ACTIONS) {
        device_music_ui_draw_menu(s_scr, "密码操作。", NULL, s_actions, 5, s_action);
    } else if (s_page == PAGE_READY) {
        device_music_ui_draw_success(s_scr, "网络已连接。", s_detail);
        device_music_ui_set_footer(s_scr, "—", s_finish_requested ? "等待" : "进入音乐", "—");
    } else if (s_page == PAGE_FAILED) {
        device_music_ui_draw_message(s_scr, DEVICE_MUSIC_SCREEN_SETUP_FAILED, "网络连接失败。", s_detail);
        device_music_ui_set_footer(s_scr, "—", "重试", "换网络");
    } else {
        device_music_ui_draw_progress(s_scr, DEVICE_MUSIC_SCREEN_REGISTERING,
            s_page == PAGE_SCANNING ? "正在扫描网络。" : "正在连接网络。",
            s_page == PAGE_SCANNING ? "正在搜索附近的 2.4 GHz Wi-Fi" : "请稍候，连接成功后保存网络", 1);
    }
    lv_screen_load(s_scr);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    ESP_LOGI(TAG, "Local Wi-Fi UI page=%u; lv_free=%u; lv_largest=%u",
             (unsigned)s_page, (unsigned)memory.free_size, (unsigned)memory.free_biggest_size);
}
void wifi_provisioning_page_refresh_display(void)
{
    s_dirty=true;render();
}
static void tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_finish_requested && s_worker_done) {
        s_worker_task = NULL;
        void (*callback)(void) = s_complete_callback;
        s_finish_requested = false;
        if (callback) callback();
        return;
    }
    render();
}
void wifi_provisioning_page_enter(void)
{
    (void)load_config(&s_config);
    s_setup_requested = 0;
    s_page = PAGE_SCANNING; s_dirty = true; s_detail[0] = 0;
    s_count = s_selected = 0; s_finish_requested = s_exit_requested = s_worker_done = false;
    s_job = JOB_NONE;
    wifi_password_editor_init(&s_editor);
    render();
    s_timer = lv_timer_create(tick, 100, NULL);
    request_job(JOB_SCAN);
}
bool wifi_provisioning_page_has_saved_config(void)
{
    if (demo_radio_nvs_prepare() != ESP_OK) return false;
    provisioning_config_t config;
    return load_config(&config);
}
void wifi_provisioning_page_set_complete_callback(void (*callback)(void)) { s_complete_callback = callback; }
void wifi_provisioning_page_exit(void)
{
    /* Normal handoff occurs only after the worker has released Wi-Fi. */
    s_exit_requested = true;
    if (s_worker_task && !s_worker_done) xTaskNotifyGive(s_worker_task);
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    memset(&s_editor, 0, sizeof(s_editor));
    memset(&s_attempt, 0, sizeof(s_attempt));
}
static void submit_password(void)
{
    if (!wifi_provisioning_password_is_valid(s_editor.text) || !s_editor.text[0]) {
        copy_limited(s_detail, sizeof(s_detail), "密码需 8–63 字符或 64 位十六进制");
        s_page = PAGE_PASSWORD; s_dirty = true; return;
    }
    copy_limited(s_attempt.password, sizeof(s_attempt.password), s_editor.text);
    request_job(JOB_CONNECT);
}
void wifi_provisioning_page_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if ((ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) || s_finish_requested) return;
    bool hold = ev == BSP_BTN_LONG;
    if (s_page == PAGE_CONNECTING || s_page == PAGE_SCANNING) return;
    if (s_page == PAGE_READY) {
        if (btn == BSP_BTN_OK && !hold && s_worker_task) {
            s_finish_requested = s_exit_requested = true; s_dirty = true;
            xTaskNotifyGive(s_worker_task);
        }
        return;
    }
    if (s_page == PAGE_FAILED) {
        if (btn == BSP_BTN_DOWN || (btn == BSP_BTN_OK && hold)) request_job(JOB_SCAN);
        else if (btn == BSP_BTN_OK) request_job(JOB_CONNECT);
        return;
    }
    if (s_page == PAGE_SCAN) {
        if (btn == BSP_BTN_OK && hold) request_job(JOB_SCAN);
        else if (btn == BSP_BTN_OK && s_count) {
            wifi_auth_mode_t auth = s_aps[s_selected].auth;
            if (auth != WIFI_AUTH_OPEN && auth != WIFI_AUTH_WPA_PSK && auth != WIFI_AUTH_WPA2_PSK &&
                auth != WIFI_AUTH_WPA_WPA2_PSK && auth != WIFI_AUTH_WPA3_PSK && auth != WIFI_AUTH_WPA2_WPA3_PSK) {
                copy_limited(s_detail, sizeof(s_detail), "该网络认证方式暂不支持，请选择其他网络");
            } else {
                s_attempt = s_config;
                copy_limited(s_attempt.ssid, sizeof(s_attempt.ssid), s_aps[s_selected].ssid);
                s_attempt.password[0] = 0;
                wifi_password_editor_init(&s_editor); s_detail[0] = 0;
                if (auth == WIFI_AUTH_OPEN) request_job(JOB_CONNECT);
                else s_page = PAGE_PASSWORD;
            }
        } else if (s_count) {
            if (btn == BSP_BTN_UP) s_selected = (s_selected + s_count - 1) % s_count;
            if (btn == BSP_BTN_DOWN) s_selected = (s_selected + 1) % s_count;
        }
    } else if (s_page == PAGE_PASSWORD) {
        s_detail[0] = 0;
        if (btn == BSP_BTN_OK && hold) { s_page = PAGE_ACTIONS; s_action = 0; }
        else if (btn == BSP_BTN_OK) {
            if (!wifi_password_editor_add(&s_editor)) copy_limited(s_detail, sizeof(s_detail), "密码已满，请删除或连接");
        } else wifi_password_editor_move(&s_editor, btn == BSP_BTN_UP ? -1 : 1, hold);
    } else if (s_page == PAGE_ACTIONS) {
        if (btn == BSP_BTN_OK && hold) s_page = PAGE_PASSWORD;
        else if (btn == BSP_BTN_UP) s_action = (s_action + 4) % 5;
        else if (btn == BSP_BTN_DOWN) s_action = (s_action + 1) % 5;
        else if (btn == BSP_BTN_OK) switch (s_action) {
        case 0: submit_password(); break;
        case 1: wifi_password_editor_delete(&s_editor); s_page = PAGE_PASSWORD; break;
        case 2: s_editor.visible = !s_editor.visible; s_page = PAGE_PASSWORD; break;
        case 3: s_page = PAGE_PASSWORD; break;
        case 4: memset(&s_editor, 0, sizeof(s_editor)); s_page = PAGE_SCAN; break;
        }
    }
    s_dirty = true;
}
