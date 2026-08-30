#include "netease_app.h"

#include "audio_stream.h"
#include "bsp_display.h"
#include "device_music_ui.h"
#include "demo_radio.h"
#include "netease_app_logic.h"
#include "netease_client.h"
#include "wifi_provisioning_logic.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NETEASE_APP_NAMESPACE "cloud"
#define NETEASE_APP_AP_PREFIX "AI-Passport"
#define NETEASE_APP_SSID_LEN 33
#define NETEASE_APP_PASSWORD_LEN 65
#define NETEASE_APP_VISIBLE_ROWS 5
#define NETEASE_APP_PLAY_BITRATE 128000
#define NETEASE_APP_PLAY_BITRATE_LABEL "128 kbps"
#define NETEASE_APP_CLICK_DEBOUNCE_MS 220
#define NETEASE_APP_BUTTON_COUNT 3
#define NETEASE_APP_TRACK_PAGE_SIZE NETEASE_CLIENT_MAX_TRACKS
#define NETEASE_APP_VOLUME_STEP 5

typedef struct {
    char ssid[NETEASE_APP_SSID_LEN];
    char password[NETEASE_APP_PASSWORD_LEN];
    char cloud_url[NETEASE_CLIENT_BASE_URL_LEN];
    char device_id[NETEASE_CLIENT_DEVICE_ID_LEN];
    char device_secret[NETEASE_CLIENT_DEVICE_SECRET_LEN];
    bool registered;
} netease_saved_config_t;

typedef enum {
    NETEASE_WORK_NONE = 0,
    NETEASE_WORK_START_QR,
    NETEASE_WORK_SWITCH_ACCOUNT,
    NETEASE_WORK_LOAD_TRACKS,
    NETEASE_WORK_LOAD_TRACK_URL,
} netease_work_t;

static const char *TAG = "netease_app";

static lv_obj_t *s_scr;
static lv_obj_t *s_qr;
static lv_timer_t *s_timer;
static TaskHandle_t s_worker_task;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_any_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_ready;
static bool s_wifi_started;
static volatile bool s_sta_got_ip;
static volatile netease_work_t s_work;
static volatile esp_err_t s_error;
static volatile bool s_exit_requested;
static volatile bool s_worker_busy;
static netease_saved_config_t s_saved;
static netease_client_t s_client;
static netease_app_logic_t s_logic;
static netease_playlist_list_t s_playlists;
static netease_track_list_t s_tracks;
static netease_track_url_t s_track_url;
static char s_message[192];
static char s_qr_payload[NETEASE_CLIENT_QR_PAYLOAD_LEN];
static char s_rendered_qr_payload[NETEASE_CLIENT_QR_PAYLOAD_LEN];
static char s_rendered_message[192];
static char s_ap_ssid[32];
static char s_track_heading[NETEASE_CLIENT_NAME_LEN];
static int64_t s_track_playlist_id;
static uint32_t s_track_offset;
static device_music_screen_t s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
static size_t s_rendered_selected = (size_t)-1;
static bool s_rendered_playing;
static bool s_rendered_volume_overlay;
static int s_rendered_volume_percent = -1;
static uint32_t s_rendered_elapsed_seconds = UINT32_MAX;
static device_music_ui_player_widgets_t s_player_widgets;
static device_music_ui_volume_widgets_t s_volume_widgets;
static TickType_t s_last_click_tick[NETEASE_APP_BUTTON_COUNT];
static bool s_ignore_click_after_long[NETEASE_APP_BUTTON_COUNT];
static void (*s_reset_callback)(void);

static void render_netease_ui(void);

static void copy_limited(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static const char *qr_state_name(netease_qr_state_t state)
{
    switch (state) {
    case NETEASE_QR_WAITING:
        return "waiting";
    case NETEASE_QR_SCANNED:
        return "scanned";
    case NETEASE_QR_CONFIRMED:
        return "confirmed";
    case NETEASE_QR_EXPIRED:
        return "expired";
    case NETEASE_QR_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static const char *screen_name(device_music_screen_t screen)
{
    switch (screen) {
    case DEVICE_MUSIC_SCREEN_QR_LOGIN:
        return "qr_login";
    case DEVICE_MUSIC_SCREEN_LOGIN_WAITING:
        return "login_waiting";
    case DEVICE_MUSIC_SCREEN_PLAYLISTS:
        return "playlists";
    case DEVICE_MUSIC_SCREEN_TRACKS:
        return "tracks";
    case DEVICE_MUSIC_SCREEN_PLAYING:
        return "playing";
    case DEVICE_MUSIC_SCREEN_PAUSED:
        return "paused";
    case DEVICE_MUSIC_SCREEN_PLAY_FAILED:
        return "failed";
    default:
        return "other";
    }
}

static const char *work_name(netease_work_t work)
{
    switch (work) {
    case NETEASE_WORK_START_QR:
        return "start_qr";
    case NETEASE_WORK_SWITCH_ACCOUNT:
        return "switch_account";
    case NETEASE_WORK_LOAD_TRACKS:
        return "load_tracks";
    case NETEASE_WORK_LOAD_TRACK_URL:
        return "load_track_url";
    case NETEASE_WORK_NONE:
    default:
        return "none";
    }
}

static void refresh_ui_from_worker(void)
{
    if (bsp_lvgl_lock(1000)) {
        render_netease_ui();
        bsp_lvgl_unlock();
    } else {
        ESP_LOGW(TAG, "Skip UI refresh: LVGL lock timeout");
    }
}

static esp_err_t nvs_read_str(nvs_handle_t nvs, const char *key, char *value, size_t value_len)
{
    size_t required = value_len;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    if (err != ESP_OK) value[0] = '\0';
    return err;
}

static bool load_saved_config(netease_saved_config_t *config)
{
    memset(config, 0, sizeof(*config));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NETEASE_APP_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return false;

    err = nvs_read_str(nvs, "ssid", config->ssid, sizeof(config->ssid));
    if (err == ESP_OK) err = nvs_read_str(nvs, "password", config->password, sizeof(config->password));
    if (err == ESP_OK) err = nvs_read_str(nvs, "cloud_url", config->cloud_url, sizeof(config->cloud_url));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_id", config->device_id, sizeof(config->device_id));
    if (err == ESP_OK) err = nvs_read_str(nvs, "device_secret", config->device_secret, sizeof(config->device_secret));
    uint8_t registered = 0;
    if (err == ESP_OK) {
        (void)nvs_get_u8(nvs, "registered", &registered);
        config->registered = registered == 1;
    }
    nvs_close(nvs);

    return err == ESP_OK &&
	           wifi_provisioning_has_ready_config(config->cloud_url,
	                                              config->device_id,
	                                              config->device_secret,
	                                              config->registered);
}

static void clear_saved_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NETEASE_APP_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    memset(&s_saved, 0, sizeof(s_saved));
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

static esp_err_t start_wifi(void)
{
    if (s_wifi_started) return ESP_OK;

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    bool local_ap_mode = s_saved.ssid[0] == '\0';
    if (local_ap_mode) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) return ESP_ERR_NO_MEM;
    } else {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) return ESP_ERR_NO_MEM;
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

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    if (local_ap_mode) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-MUSIC", NETEASE_APP_AP_PREFIX);
        wifi_config_t ap_config = {0};
        copy_limited((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), s_ap_ssid);
        ap_config.ap.ssid_len = strlen(s_ap_ssid);
        ap_config.ap.channel = 1;
        ap_config.ap.max_connection = 2;
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        err = esp_wifi_set_mode(WIFI_MODE_AP);
        if (err != ESP_OK) return err;
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        if (err != ESP_OK) return err;
    } else {
        wifi_config_t sta_config = {0};
        copy_limited((char *)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), s_saved.ssid);
        copy_limited((char *)sta_config.sta.password, sizeof(sta_config.sta.password), s_saved.password);
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) return err;
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;

    if (local_ap_mode) {
        return ESP_OK;
    }

    s_sta_got_ip = false;
    err = esp_wifi_connect();
    if (err != ESP_OK) return err;
    for (int i = 0; i < 200; i++) {
        if (s_sta_got_ip) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t sync_time_if_possible(void)
{
    time_t now = time(NULL);
    if (now > 1609459200) return ESP_OK;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    for (int i = 0; i < 100; i++) {
        now = time(NULL);
        if (now > 1609459200) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static void stop_wifi(void)
{
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

static esp_err_t ensure_client_ready(void)
{
    if (!load_saved_config(&s_saved)) {
        copy_limited(s_message, sizeof(s_message), "No relay setup\nOpen setup page first");
        refresh_ui_from_worker();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Relay setup loaded: url=%s", s_saved.cloud_url);
    copy_limited(s_message, sizeof(s_message), "Connecting Wi-Fi...");
    refresh_ui_from_worker();
    esp_err_t err = start_wifi();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi ready");
    copy_limited(s_message, sizeof(s_message), "Syncing time...");
    refresh_ui_from_worker();
    (void)sync_time_if_possible();

    netease_client_config_t config = {
        .base_url = s_saved.cloud_url,
        .device_id = s_saved.device_id,
        .device_secret = s_saved.device_secret,
        .timeout_ms = 10000,
    };
    err = netease_client_init(&s_client, &config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NetEase relay client ready");
    }
    return err;
}

static esp_err_t load_playlists(void)
{
    copy_limited(s_message, sizeof(s_message), "Loading playlists...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Loading playlists");
    esp_err_t err = netease_client_get_playlists(&s_client, &s_playlists);
    if (err == ESP_OK) {
        netease_app_logic_set_playlists(&s_logic, s_playlists.count);
        ESP_LOGI(TAG, "Playlists loaded: %u", (unsigned)s_playlists.count);
        refresh_ui_from_worker();
    }
    return err;
}

static esp_err_t run_qr_login(void)
{
    esp_err_t err = ensure_client_ready();
    if (err != ESP_OK) return err;

    copy_limited(s_message, sizeof(s_message), "Starting QR login...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Starting QR login");
    netease_qr_start_t qr = {0};
    err = netease_client_qr_start(&s_client, &qr);
    if (err != ESP_OK) return err;

    copy_limited(s_qr_payload, sizeof(s_qr_payload), qr.qr_payload);
    s_logic.state = NETEASE_APP_LOGIN_QR;
    copy_limited(s_message, sizeof(s_message), "Open NetEase app\nScan the QR payload");
    ESP_LOGI(TAG, "QR login started: login_id=%s expires=%d payload_len=%u",
             qr.login_id,
             qr.expires_in_seconds,
             (unsigned)strlen(s_qr_payload));
    refresh_ui_from_worker();

    int max_polls = qr.expires_in_seconds > 0 ? qr.expires_in_seconds / 2 : 90;
    if (max_polls <= 0) max_polls = 90;
    netease_qr_state_t last_state = NETEASE_QR_FAILED;
    for (int i = 0; i < max_polls; i++) {
        netease_qr_status_t status = {0};
        err = netease_client_qr_status(&s_client, qr.login_id, &status);
        if (err != ESP_OK) return err;
        if (i == 0 || status.state != last_state) {
            ESP_LOGI(TAG, "QR status: %s user_id=%" PRId64, qr_state_name(status.state), status.netease_user_id);
            last_state = status.state;
        }
        if (status.state == NETEASE_QR_CONFIRMED) {
            copy_limited(s_message, sizeof(s_message), "Login confirmed\nLoading playlists");
            refresh_ui_from_worker();
            return load_playlists();
        }
        if (status.state == NETEASE_QR_EXPIRED) {
            copy_limited(s_message, sizeof(s_message), "QR expired\nPress OK to retry");
            return ESP_ERR_TIMEOUT;
        }
        if (status.state == NETEASE_QR_FAILED) {
            copy_limited(s_message, sizeof(s_message), "QR login failed\nPress OK to retry");
            return ESP_FAIL;
        }
        s_logic.state = NETEASE_APP_LOGIN_WAITING;
        if (status.state == NETEASE_QR_SCANNED) {
            copy_limited(s_message, sizeof(s_message), "Scanned\nConfirm on phone");
        } else {
            copy_limited(s_message, sizeof(s_message), "Waiting for scan");
        }
        refresh_ui_from_worker();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    copy_limited(s_message, sizeof(s_message), "QR timeout\nPress OK to retry");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t switch_account(void)
{
    audio_stream_request_stop();
    copy_limited(s_message, sizeof(s_message), "Signing out...");
    refresh_ui_from_worker();

    esp_err_t err = netease_client_logout(&s_client);
    if (err != ESP_OK) return err;

    memset(&s_playlists, 0, sizeof(s_playlists));
    memset(&s_tracks, 0, sizeof(s_tracks));
    memset(&s_track_url, 0, sizeof(s_track_url));
    s_qr_payload[0] = '\0';
    s_track_heading[0] = '\0';
    s_track_playlist_id = 0;
    s_track_offset = 0;
    netease_app_logic_init(&s_logic);
    return run_qr_login();
}

static esp_err_t load_tracks(void)
{
    audio_stream_stop();
    if (s_track_playlist_id <= 0) {
        size_t index = netease_app_logic_selected(&s_logic);
        if (index >= s_playlists.count) return ESP_ERR_INVALID_ARG;
        s_track_playlist_id = s_playlists.items[index].id;
        copy_limited(s_track_heading, sizeof(s_track_heading), s_playlists.items[index].name);
        s_track_offset = 0;
    }

    copy_limited(s_message, sizeof(s_message), "Loading songs...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Loading tracks for playlist=%" PRId64 " offset=%" PRIu32,
             s_track_playlist_id,
             s_track_offset);
    esp_err_t err = netease_client_get_tracks(&s_client,
                                              s_track_playlist_id,
                                              NETEASE_APP_TRACK_PAGE_SIZE,
                                              s_track_offset,
                                              &s_tracks);
    if (err == ESP_OK) {
        netease_app_logic_set_tracks(&s_logic, s_tracks.count);
        ESP_LOGI(TAG, "Tracks loaded: %u next=%s offset=%d",
                 (unsigned)s_tracks.count,
                 s_tracks.has_next_offset ? "yes" : "no",
                 s_tracks.has_next_offset ? s_tracks.next_offset : -1);
        refresh_ui_from_worker();
    }
    return err;
}

static esp_err_t load_track_url(void)
{
    size_t index = netease_app_logic_selected(&s_logic);
    if (index >= s_tracks.count) return ESP_ERR_INVALID_ARG;

    copy_limited(s_message, sizeof(s_message), "Loading play URL...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Loading play URL for track=%" PRId64, s_tracks.items[index].id);
    esp_err_t err = netease_client_get_track_url(&s_client, s_tracks.items[index].id, NETEASE_APP_PLAY_BITRATE, &s_track_url);
    if (err == ESP_OK) {
        err = audio_stream_start(s_track_url.url);
        if (err == ESP_OK) {
            netease_app_logic_set_playing(&s_logic);
            snprintf(s_message, sizeof(s_message), "Playing\n%s", s_tracks.items[index].name);
            ESP_LOGI(TAG, "Playback started: track=%" PRId64, s_tracks.items[index].id);
            refresh_ui_from_worker();
        }
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
        netease_work_t work = s_work;
        s_work = NETEASE_WORK_NONE;
        if (work == NETEASE_WORK_NONE) {
            continue;
        }
        s_worker_busy = true;
        ESP_LOGI(TAG, "Work started: %s", work_name(work));

        esp_err_t err = ESP_OK;
        if (work == NETEASE_WORK_START_QR) {
            err = run_qr_login();
        } else if (work == NETEASE_WORK_SWITCH_ACCOUNT) {
            err = switch_account();
        } else if (work == NETEASE_WORK_LOAD_TRACKS) {
            err = load_tracks();
        } else if (work == NETEASE_WORK_LOAD_TRACK_URL) {
            err = load_track_url();
        }

        if (err != ESP_OK) {
            s_error = err;
            ESP_LOGE(TAG, "NetEase app failed: %s", esp_err_to_name(err));
            netease_app_logic_set_error(&s_logic);
            if (s_message[0] == '\0') {
                snprintf(s_message, sizeof(s_message), "Failed\n%s", esp_err_to_name(err));
            }
            refresh_ui_from_worker();
        }
        ESP_LOGI(TAG, "Work finished: %s err=%s", work_name(work), esp_err_to_name(err));
        s_worker_busy = false;
    }
    stop_wifi();
    s_worker_task = NULL;
    s_exit_requested = false;
    vTaskDelete(NULL);
}

static void request_work(netease_work_t work)
{
    if (work == NETEASE_WORK_NONE) return;
    if (s_worker_busy || s_work != NETEASE_WORK_NONE) {
        ESP_LOGW(TAG, "Ignore duplicate work: %s busy=%d queued=%s",
                 work_name(work),
                 s_worker_busy,
                 work_name(s_work));
        return;
    }
    s_work = work;
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

static device_music_screen_t screen_for_state(void)
{
    switch (netease_app_logic_state(&s_logic)) {
    case NETEASE_APP_LOGIN_QR:
        return DEVICE_MUSIC_SCREEN_QR_LOGIN;
    case NETEASE_APP_LOGIN_WAITING:
        return DEVICE_MUSIC_SCREEN_LOGIN_WAITING;
    case NETEASE_APP_PLAYLISTS:
        return DEVICE_MUSIC_SCREEN_PLAYLISTS;
    case NETEASE_APP_TRACKS:
        return DEVICE_MUSIC_SCREEN_TRACKS;
    case NETEASE_APP_PLAYING:
        return netease_app_logic_is_playing(&s_logic) ? DEVICE_MUSIC_SCREEN_PLAYING : DEVICE_MUSIC_SCREEN_PAUSED;
    case NETEASE_APP_ERROR:
    default:
        return DEVICE_MUSIC_SCREEN_PLAY_FAILED;
    }
}

static size_t visible_first(size_t selected, size_t count)
{
    if (count <= NETEASE_APP_VISIBLE_ROWS || selected < NETEASE_APP_VISIBLE_ROWS) {
        return 0;
    }
    return selected - NETEASE_APP_VISIBLE_ROWS + 1;
}

static void draw_playlists(lv_obj_t *screen)
{
    device_music_ui_row_t rows[NETEASE_APP_VISIBLE_ROWS] = {0};
    char subs[NETEASE_APP_VISIBLE_ROWS][24] = {{0}};
    size_t selected = netease_app_logic_selected(&s_logic);
    size_t first = visible_first(selected, s_playlists.count);
    size_t count = 0;

    for (size_t row = 0; row < NETEASE_APP_VISIBLE_ROWS && first + row < s_playlists.count; row++) {
        size_t index = first + row;
        rows[row].title = s_playlists.items[index].name;
        snprintf(subs[row], sizeof(subs[row]), "%d 首", s_playlists.items[index].track_count);
        rows[row].sub = subs[row];
        rows[row].selected = index == selected;
        count++;
    }

    char count_label[16];
    snprintf(count_label, sizeof(count_label), "%u/%u", (unsigned)(selected + 1), (unsigned)s_playlists.count);
    device_music_ui_draw_list(screen, DEVICE_MUSIC_SCREEN_PLAYLISTS, "选择歌单", count_label, rows, count);
}

static void draw_tracks(lv_obj_t *screen)
{
    device_music_ui_row_t rows[NETEASE_APP_VISIBLE_ROWS] = {0};
    size_t selected = netease_app_logic_selected(&s_logic);
    size_t first = visible_first(selected, s_tracks.count);
    size_t count = 0;

    for (size_t row = 0; row < NETEASE_APP_VISIBLE_ROWS && first + row < s_tracks.count; row++) {
        size_t index = first + row;
        rows[row].title = s_tracks.items[index].name;
        rows[row].sub = s_tracks.items[index].artist;
        rows[row].selected = index == selected;
        count++;
    }

    char count_label[16];
    snprintf(count_label,
             sizeof(count_label),
             "%" PRIu32 "%s",
             s_track_offset + (uint32_t)selected + 1U,
             s_tracks.has_next_offset ? "+" : "");
    device_music_ui_draw_list(screen,
                              DEVICE_MUSIC_SCREEN_TRACKS,
                              s_track_heading[0] ? s_track_heading : "歌曲列表",
                              count_label,
                              rows,
                              count);
}

static int selected_track_duration_ms(void)
{
    size_t selected = netease_app_logic_selected(&s_logic);
    if (selected >= s_tracks.count) return 0;
    return s_tracks.items[selected].duration_ms;
}

static void draw_player(lv_obj_t *screen, bool playing)
{
    size_t selected = netease_app_logic_selected(&s_logic);
    const char *song = "城市灯光";
    const char *artist = playing ? "落日飞车 · " NETEASE_APP_PLAY_BITRATE_LABEL : "播放已暂停";
    char artist_line[128];
    int elapsed_ms = (int)audio_stream_elapsed_ms();
    int duration_ms = selected_track_duration_ms();
    int progress_percent = device_music_ui_progress_percent(elapsed_ms, duration_ms);

    if (selected < s_tracks.count) {
        song = s_tracks.items[selected].name;
        if (playing) {
            snprintf(artist_line, sizeof(artist_line), "%s · " NETEASE_APP_PLAY_BITRATE_LABEL,
                     s_tracks.items[selected].artist[0] ? s_tracks.items[selected].artist : "网易云音乐");
            artist = artist_line;
        }
    }
    device_music_ui_draw_player(screen,
                                song,
                                artist,
                                progress_percent,
                                elapsed_ms,
                                duration_ms,
                                !playing,
                                &s_player_widgets);
}

static void update_player_progress_from_audio(void)
{
    device_music_screen_t screen = screen_for_state();
    if (screen != DEVICE_MUSIC_SCREEN_PLAYING && screen != DEVICE_MUSIC_SCREEN_PAUSED) {
        return;
    }
    uint32_t elapsed_seconds = audio_stream_elapsed_ms() / 1000U;
    if (elapsed_seconds == s_rendered_elapsed_seconds) {
        return;
    }
    int elapsed_ms = (int)audio_stream_elapsed_ms();
    int duration_ms = selected_track_duration_ms();
    int progress_percent = device_music_ui_progress_percent(elapsed_ms, duration_ms);
    device_music_ui_update_player_progress(&s_player_widgets, progress_percent, elapsed_ms, duration_ms);
    s_rendered_elapsed_seconds = elapsed_seconds;
}

static bool needs_rerender(device_music_screen_t screen)
{
    bool player_screen = screen == DEVICE_MUSIC_SCREEN_PLAYING || screen == DEVICE_MUSIC_SCREEN_PAUSED;
    bool volume_overlay = netease_app_logic_volume_overlay(&s_logic);
    return s_scr == NULL ||
           s_rendered_screen != screen ||
           s_rendered_selected != netease_app_logic_selected(&s_logic) ||
           s_rendered_playing != netease_app_logic_is_playing(&s_logic) ||
           (player_screen && s_rendered_volume_overlay != volume_overlay) ||
           strcmp(s_rendered_message, s_message) != 0 ||
           strcmp(s_rendered_qr_payload, s_qr_payload) != 0;
}

static void render_netease_ui(void)
{
    device_music_screen_t screen_id = screen_for_state();
    if (!needs_rerender(screen_id)) {
        update_player_progress_from_audio();
        return;
    }

    ESP_LOGI(TAG, "Render screen: %s message=\"%s\"", screen_name(screen_id), s_message);
    lv_obj_t *screen = s_scr;
    bool first_screen = screen == NULL;
    if (first_screen) {
        screen = device_music_ui_create_screen(screen_id);
    } else if (!device_music_ui_reset_screen(screen, screen_id)) {
        ESP_LOGE(TAG, "Reset screen failed: %s", screen_name(screen_id));
        return;
    }
    if (screen == NULL) {
        ESP_LOGE(TAG, "Create screen failed: %s", screen_name(screen_id));
        return;
    }
    s_qr = NULL;
    s_player_widgets.progress_fill = NULL;
    s_player_widgets.elapsed_label = NULL;
    s_player_widgets.duration_label = NULL;
    s_volume_widgets.value_label = NULL;
    s_volume_widgets.progress_fill = NULL;

    switch (screen_id) {
    case DEVICE_MUSIC_SCREEN_QR_LOGIN:
        s_qr = device_music_ui_draw_qr(screen, screen_id, NULL, NULL);
        break;
    case DEVICE_MUSIC_SCREEN_LOGIN_WAITING:
        device_music_ui_draw_confirm(screen, NULL, NULL, &s_qr);
        break;
    case DEVICE_MUSIC_SCREEN_PLAYLISTS:
        draw_playlists(screen);
        break;
    case DEVICE_MUSIC_SCREEN_TRACKS:
        draw_tracks(screen);
        break;
    case DEVICE_MUSIC_SCREEN_PLAYING:
        draw_player(screen, true);
        break;
    case DEVICE_MUSIC_SCREEN_PAUSED:
        draw_player(screen, false);
        break;
    case DEVICE_MUSIC_SCREEN_PLAY_FAILED:
        device_music_ui_draw_error(screen, screen_id, NULL, NULL);
        break;
    default:
        device_music_ui_draw_message(screen, screen_id, NULL, NULL);
        break;
    }

    if ((screen_id == DEVICE_MUSIC_SCREEN_PLAYING || screen_id == DEVICE_MUSIC_SCREEN_PAUSED) &&
        netease_app_logic_volume_overlay(&s_logic)) {
        device_music_ui_draw_volume_overlay(screen, audio_stream_volume(), &s_volume_widgets);
    }

    if (s_qr != NULL && s_qr_payload[0] != '\0') {
        lv_result_t result = lv_qrcode_update(s_qr, s_qr_payload, strlen(s_qr_payload));
        if (result != LV_RESULT_OK) {
            copy_limited(s_message, sizeof(s_message), "二维码生成失败");
            netease_app_logic_set_error(&s_logic);
        }
    }

    if (first_screen) {
        s_scr = screen;
        lv_screen_load(s_scr);
    }
    s_rendered_screen = screen_id;
    s_rendered_selected = netease_app_logic_selected(&s_logic);
    s_rendered_playing = netease_app_logic_is_playing(&s_logic);
    s_rendered_volume_overlay = netease_app_logic_volume_overlay(&s_logic);
    s_rendered_volume_percent = audio_stream_volume();
    s_rendered_elapsed_seconds = audio_stream_elapsed_ms() / 1000U;
    copy_limited(s_rendered_message, sizeof(s_rendered_message), s_message);
    copy_limited(s_rendered_qr_payload, sizeof(s_rendered_qr_payload), s_qr_payload);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    render_netease_ui();
}

void netease_app_enter(void)
{
    memset(&s_playlists, 0, sizeof(s_playlists));
    memset(&s_tracks, 0, sizeof(s_tracks));
    memset(&s_track_url, 0, sizeof(s_track_url));
    memset(&s_client, 0, sizeof(s_client));
    s_message[0] = '\0';
    s_qr_payload[0] = '\0';
    s_track_heading[0] = '\0';
    s_track_playlist_id = 0;
    s_track_offset = 0;
    s_rendered_qr_payload[0] = '\0';
    s_rendered_message[0] = '\0';
    s_error = ESP_OK;
    s_work = NETEASE_WORK_NONE;
    s_worker_busy = false;
    s_exit_requested = false;
    s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
    s_rendered_selected = (size_t)-1;
    s_rendered_playing = false;
    s_rendered_volume_overlay = false;
    s_rendered_volume_percent = -1;
    s_rendered_elapsed_seconds = UINT32_MAX;
    memset(&s_player_widgets, 0, sizeof(s_player_widgets));
    memset(&s_volume_widgets, 0, sizeof(s_volume_widgets));
    memset(s_last_click_tick, 0, sizeof(s_last_click_tick));
    memset(s_ignore_click_after_long, 0, sizeof(s_ignore_click_after_long));
    netease_app_logic_init(&s_logic);

    render_netease_ui();
    s_timer = lv_timer_create(tick, 250, NULL);

    if (s_worker_task == NULL) {
        xTaskCreate(worker_task, "netease_app", 7168, NULL, 4, &s_worker_task);
    }
    request_work(NETEASE_WORK_START_QR);
}

void netease_app_exit(void)
{
    audio_stream_stop();
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
        s_qr = NULL;
    }
}

void netease_app_set_reset_callback(void (*callback)(void))
{
    s_reset_callback = callback;
}

static netease_app_button_t map_button(bsp_btn_t btn)
{
    if (btn == BSP_BTN_UP) return NETEASE_APP_BTN_UP;
    if (btn == BSP_BTN_DOWN) return NETEASE_APP_BTN_DOWN;
    return NETEASE_APP_BTN_OK;
}

static bool map_event(bsp_btn_ev_t ev, netease_app_button_event_t *out)
{
    if (out == NULL) return false;
    if (ev == BSP_BTN_CLICK) {
        *out = NETEASE_APP_BTN_CLICK;
        return true;
    }
    if (ev == BSP_BTN_LONG) {
        *out = NETEASE_APP_BTN_LONG;
        return true;
    }
    return false;
}

static void prepare_track_load_from_selected_playlist(void)
{
    size_t index = netease_app_logic_selected(&s_logic);
    if (index >= s_playlists.count) return;
    audio_stream_request_stop();
    s_track_playlist_id = s_playlists.items[index].id;
    s_track_offset = 0;
    copy_limited(s_track_heading, sizeof(s_track_heading), s_playlists.items[index].name);
    memset(&s_tracks, 0, sizeof(s_tracks));
}

static bool try_request_track_page(netease_app_button_t button, netease_app_button_event_t event)
{
    if (event != NETEASE_APP_BTN_CLICK || netease_app_logic_state(&s_logic) != NETEASE_APP_TRACKS) {
        return false;
    }
    size_t selected = netease_app_logic_selected(&s_logic);
    if (button == NETEASE_APP_BTN_DOWN && s_tracks.count > 0 &&
        selected + 1U >= s_tracks.count && s_tracks.has_next_offset) {
        s_track_offset = (uint32_t)s_tracks.next_offset;
        copy_limited(s_message, sizeof(s_message), "Loading next songs...");
        request_work(NETEASE_WORK_LOAD_TRACKS);
        render_netease_ui();
        return true;
    }
    if (button == NETEASE_APP_BTN_UP && selected == 0 && s_track_offset > 0) {
        s_track_offset = s_track_offset > NETEASE_APP_TRACK_PAGE_SIZE ?
                         s_track_offset - NETEASE_APP_TRACK_PAGE_SIZE :
                         0;
        copy_limited(s_message, sizeof(s_message), "Loading previous songs...");
        request_work(NETEASE_WORK_LOAD_TRACKS);
        render_netease_ui();
        return true;
    }
    return false;
}

void netease_app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if ((int)btn < 0 || (int)btn >= NETEASE_APP_BUTTON_COUNT) return;

    if (ev == BSP_BTN_LONG) {
        s_ignore_click_after_long[btn] = true;
    } else if (ev == BSP_BTN_CLICK) {
        if (s_ignore_click_after_long[btn]) {
            s_ignore_click_after_long[btn] = false;
            ESP_LOGI(TAG, "Ignore release click after long press: btn=%d", btn);
            return;
        }
        TickType_t now = xTaskGetTickCount();
        TickType_t last = s_last_click_tick[btn];
        if (last != 0 && now - last < pdMS_TO_TICKS(NETEASE_APP_CLICK_DEBOUNCE_MS)) {
            ESP_LOGI(TAG, "Ignore bounced click: btn=%d", btn);
            return;
        }
        s_last_click_tick[btn] = now;
    }

    netease_app_button_event_t event;
    if (!map_event(ev, &event)) {
        return;
    }

    netease_app_button_t button = map_button(btn);
    bool work_pending = s_worker_busy || s_work != NETEASE_WORK_NONE;
    if (work_pending) {
        netease_app_action_t pending_action = netease_app_logic_dispatch_button(&s_logic,
                                                                                button,
                                                                                event,
                                                                                true);
        if (pending_action == NETEASE_APP_ACTION_RESET_CLOUD_SETUP) {
            clear_saved_config();
            copy_limited(s_message, sizeof(s_message), "Relay setup cleared");
            esp_restart();
            return;
        }
        ESP_LOGW(TAG, "Ignore button while work is pending: busy=%d queued=%s",
                 s_worker_busy,
                 work_name(s_work));
        return;
    }

    if (try_request_track_page(button, event)) {
        return;
    }

    netease_app_action_t action = netease_app_logic_dispatch_button(&s_logic, button, event, false);
    if (action == NETEASE_APP_ACTION_RESET_CLOUD_SETUP) {
        clear_saved_config();
        copy_limited(s_message, sizeof(s_message), "Relay setup cleared");
        esp_restart();
        return;
    }
    if (action == NETEASE_APP_ACTION_START_QR) {
        request_work(NETEASE_WORK_START_QR);
    } else if (action == NETEASE_APP_ACTION_LOAD_TRACKS) {
        prepare_track_load_from_selected_playlist();
        request_work(NETEASE_WORK_LOAD_TRACKS);
    } else if (action == NETEASE_APP_ACTION_LOAD_TRACK_URL) {
        request_work(NETEASE_WORK_LOAD_TRACK_URL);
    } else if (action == NETEASE_APP_ACTION_TOGGLE_PLAYBACK) {
        audio_stream_state_t audio_state = audio_stream_state();
        esp_err_t audio_err = ESP_OK;
        if (audio_state == AUDIO_STREAM_PLAYING && !netease_app_logic_is_playing(&s_logic)) {
            audio_err = audio_stream_pause();
        } else if (audio_state == AUDIO_STREAM_PAUSED && netease_app_logic_is_playing(&s_logic)) {
            audio_err = audio_stream_resume();
        } else if (!audio_stream_is_active()) {
            s_logic.state = NETEASE_APP_TRACKS;
            s_logic.playing = false;
            copy_limited(s_message, sizeof(s_message), "Playback stopped\nPress OK to play");
            render_netease_ui();
            return;
        }
        if (audio_err == ESP_OK) {
            copy_limited(s_message, sizeof(s_message),
                         netease_app_logic_is_playing(&s_logic) ? "Playback resumed" : "Playback paused");
        } else {
            s_error = audio_err;
            copy_limited(s_message, sizeof(s_message), "Playback control failed");
            netease_app_logic_set_error(&s_logic);
        }
    } else if (action == NETEASE_APP_ACTION_VOLUME_UP || action == NETEASE_APP_ACTION_VOLUME_DOWN) {
        int delta = action == NETEASE_APP_ACTION_VOLUME_UP ? NETEASE_APP_VOLUME_STEP : -NETEASE_APP_VOLUME_STEP;
        int volume = audio_stream_adjust_volume(delta);
        device_music_ui_update_volume_overlay(&s_volume_widgets, volume);
        s_rendered_volume_percent = volume;
        ESP_LOGI(TAG, "Volume changed: %d", volume);
    } else if (action == NETEASE_APP_ACTION_SWITCH_ACCOUNT) {
        request_work(NETEASE_WORK_SWITCH_ACCOUNT);
    }
    render_netease_ui();
}
