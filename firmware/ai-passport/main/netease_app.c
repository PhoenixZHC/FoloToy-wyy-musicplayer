#include "netease_app.h"

#include "audio_stream.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "device_music_ui.h"
#include "music_lyrics.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "demo_radio.h"
#include "netease_app_logic.h"
#include "netease_client.h"
#ifdef CONFIG_NETEASE_DIRECT
#include "netease_direct_adapter.h"
#include "music_artwork.h"
#endif
#include "wifi_provisioning_logic.h"
#include "wifi_provisioning_page.h"

#include "esp_event.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef CONFIG_NETEASE_DIRECT
#define NETEASE_APP_NAMESPACE "netease_wifi"
#else
#define NETEASE_APP_NAMESPACE "cloud"
#endif
#define NETEASE_APP_SSID_LEN 33
#define NETEASE_APP_PASSWORD_LEN 65
#define NETEASE_APP_VISIBLE_ROWS 4
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
    NETEASE_WORK_RESET_WIFI,
    NETEASE_WORK_SAVE_DISPLAY,
    NETEASE_WORK_NEXT_PLAYING_PAGE,
    NETEASE_WORK_LOAD_LIBRARY,
    NETEASE_WORK_PREFETCH_TRACKS,
} netease_work_t;

static const char *TAG = "netease_app";

static lv_obj_t *s_scr;
static lv_obj_t *s_qr;
static lv_timer_t *s_timer;
static TaskHandle_t s_worker_task;
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
static lv_image_dsc_t s_cover_image;
static char *s_lyric_text;
static char s_current_lyric[256];
static const char *s_lyric_status;
static netease_track_list_t s_playing_tracks;
static netease_track_t s_now_playing;
static int64_t s_playing_playlist_id;
static uint32_t s_playing_offset;
static char s_playing_heading[NETEASE_CLIENT_NAME_LEN];
static char s_nickname[NETEASE_CLIENT_NAME_LEN];
static unsigned s_playing_number;
static netease_work_t s_retry_work;
static int s_brightness = 70;
static netease_app_state_t s_rendered_state;
static bool s_rendered_night;
static bool s_display_loaded;
static bool s_display_dirty;
static netease_work_t s_active_work;
static bool s_detached_work;
static netease_track_list_t s_worker_tracks;
static netease_track_list_t s_url_tracks;
static bool s_audio_started;
static size_t s_url_index;
static bool s_page_last;
static bool s_playback_ended;
static int64_t s_request_playlist;
static uint32_t s_request_offset;
static char s_request_heading[NETEASE_CLIENT_NAME_LEN];
static esp_err_t s_background_error;
static bool s_library_loading;
static bool s_session_expired;
static bool s_qr_expired;
static volatile bool s_qr_refresh_requested;
static bool s_browse_last;
static size_t s_request_cursor;
static int s_rendered_soc = -2;
static bool s_rendered_connected;
static unsigned s_signal_level, s_rendered_signal_level;
static int64_t s_signal_checked_us;
typedef struct { bool valid; int64_t playlist; uint32_t offset; int64_t loaded_us; netease_track_list_t tracks; } track_cache_t;
static track_cache_t s_track_cache[3];
static int64_t s_prefetch_playlist;
static uint32_t s_prefetch_offset;
static int64_t s_prefetch_retry_us;
static unsigned s_cache_next;

static char s_message[192];
static char s_qr_payload[NETEASE_CLIENT_QR_PAYLOAD_LEN];
static char s_rendered_qr_payload[NETEASE_CLIENT_QR_PAYLOAD_LEN];
static char s_rendered_message[192];
static char s_track_heading[NETEASE_CLIENT_NAME_LEN];
static int64_t s_track_playlist_id;
static uint32_t s_track_offset;
static device_music_screen_t s_rendered_screen = DEVICE_MUSIC_SCREEN_COUNT;
static size_t s_rendered_selected = (size_t)-1;
static bool s_rendered_playing;
static bool s_rendered_buffering;
static bool s_rendered_volume_overlay;
static int s_rendered_volume_percent = -1;
static uint32_t s_rendered_elapsed_seconds = UINT32_MAX;
static device_music_ui_player_widgets_t s_player_widgets;
static device_music_ui_volume_widgets_t s_volume_widgets;
static TickType_t s_last_click_tick[NETEASE_APP_BUTTON_COUNT];
static void (*s_reset_callback)(void);

static void render_netease_ui(void);

static void copy_limited(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) return;
    bool message=dst==s_message;
    if(message && !bsp_lvgl_lock(0))return;
    snprintf(dst,dst_len,"%s",src?src:"");
    if(message)bsp_lvgl_unlock();
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
    case NETEASE_WORK_LOAD_LIBRARY:return "load_library";
    case NETEASE_WORK_NEXT_PLAYING_PAGE:return "next_playing_page";
    case NETEASE_WORK_SAVE_DISPLAY:return "save_display";
    case NETEASE_WORK_RESET_WIFI:return "reset_wifi";
    case NETEASE_WORK_PREFETCH_TRACKS:return "prefetch_tracks";
    case NETEASE_WORK_NONE:
    default:return "none";
    }
}

static void refresh_ui_from_worker(void)
{
    if (s_exit_requested) return;
    if (bsp_lvgl_lock(1000)) {
        if (!s_exit_requested && s_scr) render_netease_ui();
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
#ifdef CONFIG_NETEASE_DIRECT
    nvs_close(nvs);
    return err == ESP_OK && config->ssid[0] != '\0';
#else
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
#endif
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
    if (s_wifi_started) {
        if(s_sta_got_ip)return ESP_OK;
        esp_err_t reconnect=esp_wifi_connect();
        if(reconnect!=ESP_OK && reconnect!=ESP_ERR_WIFI_CONN)return reconnect;
        for(int i=0;i<200 && !s_exit_requested;i++) {
            if(s_sta_got_ip)return ESP_OK;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;
    err = demo_radio_network_prepare();
    if (err != ESP_OK) return err;

    if (!s_saved.ssid[0]) return ESP_ERR_INVALID_STATE;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;

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
    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, s_saved.ssid, strlen(s_saved.ssid));
    memcpy(sta_config.sta.password, s_saved.password, strlen(s_saved.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;

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
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
}

static esp_err_t ensure_client_ready(void)
{
    if (!load_saved_config(&s_saved)) {
        copy_limited(s_message, sizeof(s_message), "Setup required\nOpen Wi-Fi setup first");
        refresh_ui_from_worker();
        return ESP_ERR_INVALID_STATE;
    }

    copy_limited(s_message, sizeof(s_message), "Connecting Wi-Fi...");
    refresh_ui_from_worker();
    esp_err_t err = start_wifi();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Wi-Fi ready");
    copy_limited(s_message, sizeof(s_message), "Syncing time...");
    refresh_ui_from_worker();
    err = sync_time_if_possible();
#ifdef CONFIG_NETEASE_DIRECT
    if (err != ESP_OK) return err;
    return direct_adapter_init();
#else

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
#endif
}

static esp_err_t load_playlists(void)
{
    if(bsp_lvgl_lock(0)) { s_library_loading=true;bsp_lvgl_unlock(); }
    copy_limited(s_message, sizeof(s_message), "Loading playlists...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Loading playlists");
#ifdef CONFIG_NETEASE_DIRECT
    netease_playlist_list_t *loaded=calloc(1,sizeof(*loaded));
    if(!loaded)return ESP_ERR_NO_MEM;
    esp_err_t err = direct_adapter_playlists(loaded);
#else
    netease_playlist_list_t *loaded=calloc(1,sizeof(*loaded));
    if(!loaded)return ESP_ERR_NO_MEM;
    esp_err_t err = netease_client_get_playlists(&s_client,loaded);
#endif
    if (err == ESP_OK) {
        if(bsp_lvgl_lock(0)) {
            if(!s_exit_requested) {
                s_playlists=*loaded;
                netease_app_state_t state=s_logic.state;size_t cursor=s_logic.selected,count=s_logic.item_count;
                netease_app_logic_set_playlists(&s_logic,s_playlists.count);
                if(s_detached_work) { s_logic.state=state;s_logic.selected=cursor;s_logic.item_count=count; }
            }
            s_library_loading=false;bsp_lvgl_unlock();
        }
        ESP_LOGI(TAG, "Playlists loaded: %u", (unsigned)s_playlists.count);
        refresh_ui_from_worker();
    }
    free(loaded);
    return err;
}

static esp_err_t run_qr_login(void)
{
    esp_err_t err = ensure_client_ready();
    if (err != ESP_OK) return err;
#ifdef CONFIG_NETEASE_DIRECT
    err = direct_adapter_restore();
    if(err==NETEASE_DIRECT_ERR_AUTH) { s_session_expired=true;return err; }
    if(err==ESP_OK) { direct_adapter_nickname(s_nickname,sizeof(s_nickname));return load_playlists(); }
    if (err != ESP_ERR_NOT_FOUND) return err;
#endif

    copy_limited(s_message, sizeof(s_message), "Starting QR login...");
    refresh_ui_from_worker();
    ESP_LOGI(TAG, "Starting QR login");
    netease_qr_start_t qr = {0};
#ifdef CONFIG_NETEASE_DIRECT
    err = direct_adapter_qr_start(&qr);
#else
    err = netease_client_qr_start(&s_client, &qr);
#endif
    if (err != ESP_OK) return err;

    if(bsp_lvgl_lock(0)) {
        copy_limited(s_qr_payload,sizeof(s_qr_payload),qr.qr_payload);s_logic.state=NETEASE_APP_LOGIN_QR;
        bsp_lvgl_unlock();
    }
    copy_limited(s_message, sizeof(s_message), "Open NetEase app\nScan the QR payload");
    ESP_LOGI(TAG, "QR login started: expires=%d payload_len=%u",
             qr.expires_in_seconds,
             (unsigned)strlen(s_qr_payload));
    refresh_ui_from_worker();

    int max_polls = qr.expires_in_seconds > 0 ? qr.expires_in_seconds / 2 : 90;
    if (max_polls <= 0) max_polls = 90;
    netease_qr_state_t last_state = NETEASE_QR_FAILED;
    for (int i = 0; i < max_polls; i++) {
        if (s_exit_requested) return ESP_ERR_INVALID_STATE;
        if(s_qr_refresh_requested)return ESP_OK;
        netease_qr_status_t status = {0};
#ifdef CONFIG_NETEASE_DIRECT
        err = direct_adapter_qr_poll(qr.login_id, &status);
#else
        err = netease_client_qr_status(&s_client, qr.login_id, &status);
#endif
        if (err != ESP_OK) return err;
        if (i == 0 || status.state != last_state) {
            ESP_LOGI(TAG, "QR status: %s user_id=%" PRId64, qr_state_name(status.state), status.netease_user_id);
            last_state = status.state;
        }
        if (status.state == NETEASE_QR_CONFIRMED) {
#ifdef CONFIG_NETEASE_DIRECT
            direct_adapter_nickname(s_nickname,sizeof(s_nickname));
#else
            copy_limited(s_nickname,sizeof(s_nickname),status.nickname);
#endif
            copy_limited(s_message, sizeof(s_message), "Login confirmed\nLoading playlists");
            refresh_ui_from_worker();
            if(bsp_lvgl_lock(0)) {
                s_logic.state=NETEASE_APP_SIGNED_IN;s_qr_payload[0]='\0';
                bsp_lvgl_unlock();
            }
            return ESP_OK;
        }
        if (status.state == NETEASE_QR_EXPIRED) {
            s_qr_expired=true;
            copy_limited(s_message, sizeof(s_message), "QR expired\nPress OK to retry");
            return ESP_ERR_TIMEOUT;
        }
        if (status.state == NETEASE_QR_FAILED) {
            copy_limited(s_message, sizeof(s_message), "QR login failed\nPress OK to retry");
            return ESP_FAIL;
        }
        if(bsp_lvgl_lock(0)) {
            s_logic.state=status.state==NETEASE_QR_SCANNED?NETEASE_APP_LOGIN_WAITING:NETEASE_APP_LOGIN_QR;
            bsp_lvgl_unlock();
        }
        if (status.state == NETEASE_QR_SCANNED) {
            copy_limited(s_message, sizeof(s_message), "Scanned\nConfirm on phone");
        } else {
            copy_limited(s_message, sizeof(s_message), "Waiting for scan");
        }
        refresh_ui_from_worker();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    s_qr_expired=true;
    copy_limited(s_message, sizeof(s_message), "QR timeout\nPress OK to retry");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t switch_account(void)
{
    s_audio_started=false;s_playback_ended=false;
    audio_stream_stop();
    copy_limited(s_message, sizeof(s_message), "Signing out...");
    refresh_ui_from_worker();

#ifdef CONFIG_NETEASE_DIRECT
    esp_err_t err = direct_adapter_logout();
#else
    esp_err_t err = netease_client_logout(&s_client);
#endif
    if (err != ESP_OK) return err;

    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    memset(&s_track_cache, 0, sizeof(s_track_cache));
    memset(&s_playlists, 0, sizeof(s_playlists));
    memset(&s_tracks, 0, sizeof(s_tracks));
    memset(&s_track_url, 0, sizeof(s_track_url));
    s_qr_payload[0] = '\0';
    s_track_heading[0] = '\0';
    s_track_playlist_id = 0;
    s_track_offset = 0;
    netease_app_logic_init(&s_logic);s_session_expired=false;
    memset(&s_playing_tracks,0,sizeof(s_playing_tracks));memset(&s_now_playing,0,sizeof(s_now_playing));s_nickname[0]='\0';
    bsp_lvgl_unlock();
    return run_qr_login();
}

static void publish_tracks(const netease_track_list_t *tracks)
{
    if(!bsp_lvgl_lock(0))return;
    if(s_exit_requested) { bsp_lvgl_unlock();return; }
    if(s_active_work==NETEASE_WORK_NEXT_PLAYING_PAGE) { s_url_tracks=*tracks;bsp_lvgl_unlock();return; }
    s_tracks=*tracks;s_track_playlist_id=s_request_playlist;s_track_offset=s_request_offset;
    copy_limited(s_track_heading,sizeof(s_track_heading),s_request_heading);
    netease_app_state_t state=s_logic.state;size_t selected=s_logic.selected,count=s_logic.item_count;
    s_logic.track_cursor=s_browse_last && tracks->count?tracks->count-1:s_request_cursor;
    s_browse_last=false;
    netease_app_logic_set_tracks(&s_logic,s_tracks.count);
    if(s_detached_work) { s_logic.state=state;s_logic.selected=selected;s_logic.item_count=count; }
    bsp_lvgl_unlock();
}
static esp_err_t load_tracks(void)
{
    for(size_t i=0;i<3;i++) {
        if(s_track_cache[i].valid && s_track_cache[i].playlist==s_request_playlist && s_track_cache[i].offset==s_request_offset && esp_timer_get_time()-s_track_cache[i].loaded_us<300000000) {
            publish_tracks(&s_track_cache[i].tracks);
            ESP_LOGI(TAG,"Track page cache hit: offset=%" PRIu32,s_request_offset);return ESP_OK;
        }
    }
    if(s_request_playlist<=0)return ESP_ERR_INVALID_ARG;
    if(!s_sta_got_ip) { esp_err_t reconnect=start_wifi();if(reconnect!=ESP_OK)return reconnect; }
    copy_limited(s_message,sizeof(s_message),"Loading songs...");refresh_ui_from_worker();
    esp_err_t err=ESP_FAIL;
    for(unsigned attempt=0;attempt<3;attempt++) {
#ifdef CONFIG_NETEASE_DIRECT
        err=direct_adapter_tracks(s_request_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_request_offset,&s_worker_tracks);
#else
        err=netease_client_get_tracks(&s_client,s_request_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_request_offset,&s_worker_tracks);
#endif
        if(err==ESP_OK || (err!=ESP_ERR_NO_MEM && err!=ESP_ERR_HTTP_CONNECT && err!=ESP_ERR_TIMEOUT) || attempt==2)break;
        ESP_LOGW(TAG,"Track request retry: attempt=%u err=%s free=%u largest=%u",attempt+1,esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if(err==ESP_OK && !s_exit_requested) {
        if(bsp_lvgl_lock(1000)) {
            track_cache_t *cache=&s_track_cache[s_cache_next++ % 3];
            cache->playlist=s_request_playlist;cache->offset=s_request_offset;cache->tracks=s_worker_tracks;cache->loaded_us=esp_timer_get_time();cache->valid=true;
            bsp_lvgl_unlock();
        }
        publish_tracks(&s_worker_tracks);
        ESP_LOGI(TAG,"Tracks loaded: offset=%" PRIu32 " count=%u next=%s",s_request_offset,(unsigned)s_worker_tracks.count,s_worker_tracks.has_next_offset?"yes":"no");
    }
    return err;
}

/* Called with the UI mutex held: cached metadata is copied without I/O. */
static bool track_page_cached(int64_t playlist,uint32_t offset)
{
    for(size_t i=0;i<3;i++)if(s_track_cache[i].valid && s_track_cache[i].playlist==playlist &&
        s_track_cache[i].offset==offset && esp_timer_get_time()-s_track_cache[i].loaded_us<300000000)return true;
    return false;
}
static void maybe_prefetch_tracks(void)
{
    if(s_exit_requested || s_worker_busy || s_work!=NETEASE_WORK_NONE || !s_sta_got_ip ||
       s_logic.state!=NETEASE_APP_TRACKS || esp_timer_get_time()<s_prefetch_retry_us)return;
    uint32_t target;
    if(!netease_app_prefetch_offset(s_track_offset,s_tracks.count,s_logic.selected,
        s_tracks.has_next_offset,(uint32_t)s_tracks.next_offset,NETEASE_APP_TRACK_PAGE_SIZE,&target))return;
    if(track_page_cached(s_track_playlist_id,target)) {
        if(s_tracks.has_next_offset && !track_page_cached(s_track_playlist_id,(uint32_t)s_tracks.next_offset))target=(uint32_t)s_tracks.next_offset;
        else if(s_track_offset && !track_page_cached(s_track_playlist_id,s_track_offset-NETEASE_APP_TRACK_PAGE_SIZE))target=s_track_offset-NETEASE_APP_TRACK_PAGE_SIZE;
        else return;
    }
    /* No speculative TLS when its memory budget cannot coexist with audio. */
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<49152 ||
       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24576)return;
    s_prefetch_playlist=s_track_playlist_id;s_prefetch_offset=target;
    s_work=NETEASE_WORK_PREFETCH_TRACKS;xTaskNotifyGive(s_worker_task);
}
static esp_err_t prefetch_tracks(void)
{
#ifdef CONFIG_NETEASE_DIRECT
    esp_err_t err=direct_adapter_tracks(s_prefetch_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_prefetch_offset,&s_worker_tracks);
#else
    esp_err_t err=netease_client_get_tracks(&s_client,s_prefetch_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_prefetch_offset,&s_worker_tracks);
#endif
    if(bsp_lvgl_lock(1000)) {
        if(err==ESP_OK && !s_exit_requested) {
            /* Prefer evicting a page outside current / previous / next. */
            size_t slot=s_cache_next++%3;
            for(size_t i=0;i<3;i++)if(!s_track_cache[i].valid || s_track_cache[i].playlist!=s_track_playlist_id ||
                (s_track_cache[i].offset!=s_track_offset &&
                 (s_track_offset<NETEASE_APP_TRACK_PAGE_SIZE || s_track_cache[i].offset!=s_track_offset-NETEASE_APP_TRACK_PAGE_SIZE) &&
                 (!s_tracks.has_next_offset || s_track_cache[i].offset!=(uint32_t)s_tracks.next_offset))) { slot=i;break; }
            track_cache_t *cache=&s_track_cache[slot];
            cache->playlist=s_prefetch_playlist;cache->offset=s_prefetch_offset;cache->tracks=s_worker_tracks;
            cache->loaded_us=esp_timer_get_time();cache->valid=true;
            ESP_LOGI(TAG,"Prefetch ready: offset=%" PRIu32 " count=%u",s_prefetch_offset,(unsigned)s_worker_tracks.count);
        } else if(err!=ESP_OK) {
            s_prefetch_retry_us=esp_timer_get_time()+30000000;
            ESP_LOGW(TAG,"Prefetch failed: offset=%" PRIu32 " err=%s; current page retained",s_prefetch_offset,esp_err_to_name(err));
        }
        bsp_lvgl_unlock();
    }
    return err;
}

static esp_err_t load_track_url(void)
{
    size_t index=s_url_index;
    if(index>=s_url_tracks.count)return ESP_ERR_INVALID_ARG;
    /* Release the old decoder and CDN connection before allocating API TLS. */
    audio_stream_stop();
    if (audio_stream_is_active()) return ESP_ERR_INVALID_STATE;
    if(bsp_lvgl_lock(0)) { s_logic.playing=false;s_audio_started=false;bsp_lvgl_unlock(); }

    copy_limited(s_message, sizeof(s_message), "Loading play URL...");
    refresh_ui_from_worker();
    /* Detach old widgets under the UI lock before releasing their image data. */
    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    device_music_ui_set_media(NULL,NULL);
    s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;
    lv_image_cache_drop(&s_cover_image);
    if(s_scr && !s_exit_requested)render_netease_ui();
    free((void *)s_cover_image.data);memset(&s_cover_image,0,sizeof(s_cover_image));
    free(s_lyric_text);s_lyric_text=NULL;s_current_lyric[0]=0;s_lyric_status=NULL;
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Loading play URL for track=%" PRId64, s_url_tracks.items[index].id);
#ifdef CONFIG_NETEASE_DIRECT
    esp_err_t err = direct_adapter_url(s_url_tracks.items[index].id, NETEASE_APP_PLAY_BITRATE, &s_track_url);
#else
    esp_err_t err = netease_client_get_track_url(&s_client, s_url_tracks.items[index].id, NETEASE_APP_PLAY_BITRATE, &s_track_url);
#endif
    if(err!=ESP_OK)return err;
#ifdef CONFIG_NETEASE_DIRECT
    lv_image_dsc_t cover={0};char cover_url[256];
    esp_err_t cover_err=netease_direct_get_cover_url(s_url_tracks.items[index].id,cover_url,sizeof(cover_url));
    if(cover_err==ESP_OK && !s_exit_requested)cover_err=music_artwork_load(cover_url,&cover);
    if(cover_err!=ESP_OK)ESP_LOGW(TAG,"Cover omitted: %s",esp_err_to_name(cover_err));
    char *lyrics=s_exit_requested?NULL:malloc(8192);
    esp_err_t lyric_err=lyrics?netease_direct_get_lyrics(s_url_tracks.items[index].id,lyrics,8192):ESP_ERR_NO_MEM;
    if(lyric_err!=ESP_OK) { free(lyrics);lyrics=NULL;ESP_LOGW(TAG,"Lyrics unavailable: %s",esp_err_to_name(lyric_err)); }
    if(lyrics) {
        size_t bytes=strlen(lyrics)+1;
        char *compact=realloc(lyrics,bytes);
        if(compact)lyrics=compact;
        ESP_LOGI(TAG,"Lyrics retained: bytes=%u",(unsigned)bytes);
    }
    if(!bsp_lvgl_lock(0)) { free((void *)cover.data);free(lyrics);return ESP_ERR_TIMEOUT; }
    if(s_exit_requested) { bsp_lvgl_unlock();free((void *)cover.data);free(lyrics);return ESP_ERR_INVALID_STATE; }
    s_cover_image=cover;s_lyric_text=lyrics;
    s_lyric_status=lyric_err==ESP_ERR_NOT_FOUND?"暂无歌词":lyric_err==ESP_OK?"":"歌词读取失败";
    bsp_lvgl_unlock();
#endif
    if (err == ESP_OK && !s_exit_requested) {
        err = audio_stream_start(s_track_url.url);
        if (err == ESP_OK) {
            if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
            if(s_exit_requested) { bsp_lvgl_unlock();audio_stream_stop();return ESP_ERR_INVALID_STATE; }
            netease_app_state_t view_state=s_logic.state;size_t view_selected=s_logic.selected,view_count=s_logic.item_count;
            s_now_playing=s_url_tracks.items[index];s_playing_tracks=s_url_tracks;
            s_playing_playlist_id=s_request_playlist;s_playing_offset=s_request_offset;
            s_playing_number=s_request_offset+(unsigned)index+1;
            copy_limited(s_playing_heading,sizeof(s_playing_heading),s_request_heading);
            s_logic.state=NETEASE_APP_TRACKS;s_logic.selected=index;
            size_t browse_count=s_logic.track_count,browse_cursor=s_logic.track_cursor;s_logic.track_count=s_url_tracks.count;
            netease_app_logic_set_playing(&s_logic);s_audio_started=true;s_playback_ended=false;
            if(s_detached_work) { s_logic.state=view_state;s_logic.selected=view_selected;s_logic.item_count=view_count;s_logic.track_count=browse_count;s_logic.track_cursor=browse_cursor; }
            if(!s_detached_work) { s_tracks=s_url_tracks;s_logic.track_cursor=index;s_track_playlist_id=s_request_playlist;s_track_offset=s_request_offset;copy_limited(s_track_heading,sizeof(s_track_heading),s_request_heading); }
            bsp_lvgl_unlock();
            copy_limited(s_message,sizeof(s_message),"Playback started");
            ESP_LOGI(TAG, "Playback started: track=%" PRId64, s_url_tracks.items[index].id);
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
        if(!bsp_lvgl_lock(0))continue;
        netease_work_t work=s_work;s_work=NETEASE_WORK_NONE;
        if(work==NETEASE_WORK_NONE) { bsp_lvgl_unlock();continue; }
        s_worker_busy=true;s_active_work=work;
        bsp_lvgl_unlock();
        int64_t started_us=esp_timer_get_time();
        ESP_LOGI(TAG, "Work started: %s", work_name(work));
#ifdef CONFIG_NETEASE_DIRECT
        ESP_LOGI(TAG, "Resources: free=%u largest=%u minimum=%u stack=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif

        esp_err_t err = ESP_OK;
        if(!s_display_loaded) {
            nvs_handle_t nvs;
            if(nvs_open("music_display",NVS_READONLY,&nvs)==ESP_OK) {
                uint8_t night=0,brightness=70;
                nvs_get_u8(nvs,"night",&night);nvs_get_u8(nvs,"brightness",&brightness);nvs_close(nvs);
                s_brightness=brightness>=10 && brightness<=100?brightness:70;
                if(bsp_lvgl_lock(1000)) { device_music_ui_set_night(night!=0);bsp_display_backlight(s_brightness);bsp_lvgl_unlock(); }
            }
            s_display_loaded=true;
        }
        if(work==NETEASE_WORK_PREFETCH_TRACKS) {
            err=prefetch_tracks();
        } else if (work == NETEASE_WORK_START_QR) {
            err = run_qr_login();
        } else if(work==NETEASE_WORK_LOAD_LIBRARY) {
            if(!s_sta_got_ip)err=start_wifi();
            if(err==ESP_OK)err=load_playlists();
        } else if (work == NETEASE_WORK_SWITCH_ACCOUNT) {
            err = switch_account();
        } else if (work == NETEASE_WORK_LOAD_TRACKS) {
            err = load_tracks();
        } else if(work==NETEASE_WORK_NEXT_PLAYING_PAGE) {
            err=load_tracks();
            if(err==ESP_OK && s_url_tracks.count) { s_url_index=s_page_last?s_url_tracks.count-1:0;err=load_track_url(); }
            else if(err==ESP_OK && bsp_lvgl_lock(0)) {
                s_playback_ended=true;s_logic.playing=false;
                if(!s_detached_work) { s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2; }
                bsp_lvgl_unlock();
            }
        } else if (work == NETEASE_WORK_LOAD_TRACK_URL) {
            if(!s_sta_got_ip)err=start_wifi();
            if(err==ESP_OK)err = load_track_url();
        } else if (work == NETEASE_WORK_RESET_WIFI) {
            audio_stream_stop();
            wifi_provisioning_page_request_setup();
            esp_restart();
        } else if (work == NETEASE_WORK_SAVE_DISPLAY) {
            nvs_handle_t nvs;
            err=nvs_open("music_display",NVS_READWRITE,&nvs);
            if(err==ESP_OK) {
                err=nvs_set_u8(nvs,"night",device_music_ui_is_night());
                if(err==ESP_OK)err=nvs_set_u8(nvs,"brightness",s_brightness);
                if(err==ESP_OK)err=nvs_commit(nvs);
                nvs_close(nvs);
            }
        }

        if (err != ESP_OK) {
#ifdef CONFIG_NETEASE_DIRECT
            if (err == NETEASE_DIRECT_ERR_AUTH && !s_exit_requested) {
                /* Keep the error explicit; do not steal a detached view for QR login. */
                s_session_expired=true;
                s_retry_work=NETEASE_WORK_START_QR;
            }
#endif
        }
        if (err != ESP_OK && !s_exit_requested && work!=NETEASE_WORK_PREFETCH_TRACKS) {
            s_retry_work=s_session_expired?NETEASE_WORK_START_QR:work==NETEASE_WORK_NEXT_PLAYING_PAGE && s_url_tracks.count?NETEASE_WORK_LOAD_TRACK_URL:work;
            s_error = err;
            ESP_LOGE(TAG, "NetEase app failed: %s", esp_err_to_name(err));
            if(bsp_lvgl_lock(0)) {
                if(!s_detached_work)netease_app_logic_set_error(&s_logic);else s_background_error=err;
                bsp_lvgl_unlock();
            }
            copy_limited(s_message,sizeof(s_message),esp_err_to_name(err));
#ifdef CONFIG_NETEASE_DIRECT
            if (err == NETEASE_DIRECT_ERR_LOGIN_REJECTED) {
                copy_limited(s_message, sizeof(s_message), "Login rejected (8821)\nTry again later");
            }
#endif
            refresh_ui_from_worker();
        }
        ESP_LOGI(TAG, "Work finished: %s err=%s elapsed_ms=%" PRId64, work_name(work), esp_err_to_name(err),(esp_timer_get_time()-started_us)/1000);
        if(bsp_lvgl_lock(0)) {
            s_library_loading=false;
            s_active_work=NETEASE_WORK_NONE;
            if(s_qr_refresh_requested && !s_exit_requested) {
                s_qr_refresh_requested=false;s_qr_payload[0]='\0';s_work=NETEASE_WORK_START_QR;xTaskNotifyGive(s_worker_task);
            }
            if(s_display_dirty && s_work==NETEASE_WORK_NONE) { s_display_dirty=false;s_work=NETEASE_WORK_SAVE_DISPLAY;xTaskNotifyGive(s_worker_task); }
            s_worker_busy=false;
            if(!s_exit_requested && s_scr)render_netease_ui();
            bsp_lvgl_unlock();
        }
    }
    audio_stream_stop();
    stop_wifi();
    s_worker_task = NULL;
    vTaskDelete(NULL);
}

static void request_work(netease_work_t work)
{
    if (work == NETEASE_WORK_NONE) return;
    if(work==NETEASE_WORK_SAVE_DISPLAY && (s_worker_busy || s_work!=NETEASE_WORK_NONE)) { s_display_dirty=true;return; }
    if ((s_worker_busy && s_active_work!=NETEASE_WORK_PREFETCH_TRACKS) || s_work != NETEASE_WORK_NONE) {
        ESP_LOGW(TAG, "Ignore duplicate work: %s busy=%d queued=%s",
                 work_name(work),
                 s_worker_busy,
                 work_name(s_work));
        return;
    }
    s_detached_work=false;s_background_error=ESP_OK;
    s_request_cursor=s_logic.track_cursor;
    if(work==NETEASE_WORK_START_QR || work==NETEASE_WORK_SWITCH_ACCOUNT) {
        s_qr_expired=false;s_qr_payload[0]='\0';s_logic.state=NETEASE_APP_LOGIN_QR;
    }
    s_request_playlist=s_track_playlist_id;s_request_offset=s_track_offset;
    copy_limited(s_request_heading,sizeof(s_request_heading),s_track_heading);
    if(work==NETEASE_WORK_LOAD_TRACK_URL) {
        s_url_index=s_logic.selected;
        if(s_logic.state==NETEASE_APP_PLAYING || s_logic.state==NETEASE_APP_ENDED) {
            s_url_tracks=s_playing_tracks;s_request_playlist=s_playing_playlist_id;s_request_offset=s_playing_offset;
            copy_limited(s_request_heading,sizeof(s_request_heading),s_playing_heading);
        } else s_url_tracks=s_tracks;
    }
    if(work==NETEASE_WORK_NEXT_PLAYING_PAGE)memset(&s_url_tracks,0,sizeof(s_url_tracks));
    if(work==NETEASE_WORK_LOAD_TRACKS) {
        for(size_t i=0;i<3;i++) {
            if(s_track_cache[i].valid && s_track_cache[i].playlist==s_request_playlist &&
               s_track_cache[i].offset==s_request_offset && esp_timer_get_time()-s_track_cache[i].loaded_us<300000000) {
                publish_tracks(&s_track_cache[i].tracks);
                ESP_LOGI(TAG,"Track page cache hit in UI: offset=%" PRIu32,s_request_offset);
                return;
            }
        }
    }
    s_work = work;
    if (s_worker_task != NULL) {
        xTaskNotifyGive(s_worker_task);
    }
}

static device_music_screen_t screen_for_state(void)
{
    netease_work_t loading=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
    if(!s_detached_work && (loading==NETEASE_WORK_LOAD_LIBRARY || loading==NETEASE_WORK_LOAD_TRACKS || loading==NETEASE_WORK_NEXT_PLAYING_PAGE || loading==NETEASE_WORK_LOAD_TRACK_URL ||
       ((loading==NETEASE_WORK_START_QR || loading==NETEASE_WORK_SWITCH_ACCOUNT) && !s_qr_payload[0] && s_logic.state!=NETEASE_APP_SIGNED_IN && s_logic.state!=NETEASE_APP_ERROR)))return DEVICE_MUSIC_SCREEN_LOADING_TRACK;
    switch (netease_app_logic_state(&s_logic)) {
    case NETEASE_APP_SIGNED_IN:return DEVICE_MUSIC_SCREEN_READY;
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
    case NETEASE_APP_NAVIGATION:case NETEASE_APP_SETTINGS:case NETEASE_APP_APPEARANCE:
    case NETEASE_APP_BRIGHTNESS:case NETEASE_APP_NETWORK:case NETEASE_APP_ACCOUNT:
    case NETEASE_APP_LOGOUT_CONFIRM:case NETEASE_APP_WIFI_CONFIRM:case NETEASE_APP_ABOUT:case NETEASE_APP_ENDED:
        return DEVICE_MUSIC_SCREEN_TRACKS;
    case NETEASE_APP_ERROR:
    default:
        return DEVICE_MUSIC_SCREEN_PLAY_FAILED;
    }
}

static size_t visible_first(size_t selected, size_t count)
{
    if(count<=NETEASE_APP_VISIBLE_ROWS || selected<3)return 0;
    size_t first=selected-2;
    if(first>count-NETEASE_APP_VISIBLE_ROWS)first=count-NETEASE_APP_VISIBLE_ROWS;
    return first;
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
        rows[row].number = (unsigned)index+1;
        count++;
    }

    char count_label[16];
    snprintf(count_label, sizeof(count_label), "%u/%u%s", (unsigned)(s_playlists.count?selected+1:0), (unsigned)s_playlists.count, s_playlists.truncated ? "+" : "");
    device_music_ui_draw_list(screen, DEVICE_MUSIC_SCREEN_PLAYLISTS, "我的歌单", count_label, rows, count);
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
        rows[row].number = s_track_offset+(unsigned)index+1;
        rows[row].playing = s_logic.has_player && s_tracks.items[index].id==s_now_playing.id;
        count++;
    }

    char count_label[16];
    snprintf(count_label,
             sizeof(count_label),
             "%" PRIu32 "%s",
             s_tracks.count?s_track_offset+(uint32_t)selected+1U:0,
             s_tracks.has_next_offset ? "+" : "");
    device_music_ui_draw_list(screen,
                              DEVICE_MUSIC_SCREEN_TRACKS,
                              s_track_heading[0] ? s_track_heading : "歌曲列表",
                              count_label,
                              rows,
                              count);
}

static int selected_track_duration_ms(void) { return s_now_playing.duration_ms; }
static void draw_player(lv_obj_t *screen, bool playing)
{
    int elapsed=(int)audio_stream_elapsed_ms();
    music_lyrics_line(s_lyric_text,(uint32_t)elapsed,s_current_lyric,sizeof(s_current_lyric));
    device_music_ui_set_media(s_cover_image.data?&s_cover_image:NULL,s_current_lyric[0]?s_current_lyric:s_lyric_status);
    device_music_ui_set_playback_context(s_playing_number,s_playing_heading,audio_stream_volume());
    device_music_ui_set_buffering(s_audio_started && playing && elapsed==0);
    device_music_ui_draw_player(screen,s_now_playing.name,s_now_playing.artist,
        device_music_ui_progress_percent(elapsed,s_now_playing.duration_ms),elapsed,s_now_playing.duration_ms,!playing,&s_player_widgets);
}

static void update_player_progress_from_audio(void)
{
    device_music_screen_t screen = screen_for_state();
    if (s_logic.volume_overlay || (screen != DEVICE_MUSIC_SCREEN_PLAYING && screen != DEVICE_MUSIC_SCREEN_PAUSED)) {
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
    music_lyrics_line(s_lyric_text,(uint32_t)elapsed_ms,s_current_lyric,sizeof(s_current_lyric));
    device_music_ui_update_lyric(&s_player_widgets,s_current_lyric[0]?s_current_lyric:s_lyric_status);
    s_rendered_elapsed_seconds = elapsed_seconds;
}

static bool needs_rerender(device_music_screen_t screen)
{
    bool volume_overlay = netease_app_logic_volume_overlay(&s_logic);
    return s_scr == NULL ||
           s_rendered_connected != s_sta_got_ip || s_rendered_signal_level != s_signal_level || s_rendered_soc != bsp_battery_soc() ||
           s_rendered_state != s_logic.state || s_rendered_night != device_music_ui_is_night() ||
           s_rendered_screen != screen ||
           s_rendered_selected != netease_app_logic_selected(&s_logic) ||
           s_rendered_playing != netease_app_logic_is_playing(&s_logic) ||
           s_rendered_buffering != (s_audio_started && s_logic.playing && audio_stream_elapsed_ms()==0) ||
           (s_rendered_volume_overlay != volume_overlay) ||
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
    device_music_ui_set_network(s_sta_got_ip,s_signal_level);
    device_music_ui_set_now_playing(s_background_error!=ESP_OK?"后台读取失败，请重试":s_logic.has_player?s_now_playing.name:NULL,!s_logic.playing);
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
    s_player_widgets.lyric_label = NULL;
    s_volume_widgets.value_label = NULL;
    s_volume_widgets.progress_fill = NULL;

    if (s_logic.state >= NETEASE_APP_NAVIGATION && screen_id!=DEVICE_MUSIC_SCREEN_LOADING_TRACK) {
        const char *const nav[]={"返回播放","当前歌曲列表","我的歌单","音量","设置"};
        const char *const settings[]={"显示与日夜","网络","账号","设备信息"};
        const char *const appearance[]={"日间 · 暖纸","夜间 · 深棕","背光亮度","返回设置"};
        const char *const network[]={"重新配网","返回设置"};
        const char *const account[]={"退出当前账号","返回设置"};
        const char *const logout[]={"取消，保留登录","确认退出账号"};
        const char *const reset[]={"取消，保留网络","确认重新配网"};
        const char *const ended[]={"从头播放","返回歌曲列表"};
        const char *const *items=nav;size_t n=5;const char *title="翻到哪一页？";const char *detail=NULL;
        switch(s_logic.state) {
        case NETEASE_APP_SETTINGS:items=settings;n=4;title="设置";break;
        case NETEASE_APP_APPEARANCE:items=appearance;n=4;title="显示与日夜";break;
        case NETEASE_APP_NETWORK:items=network;n=2;title="网络";detail=s_saved.ssid;break;
        case NETEASE_APP_ACCOUNT:items=account;n=2;title="账号";detail=s_nickname[0]?s_nickname:"网易云音乐账号";break;
        case NETEASE_APP_LOGOUT_CONFIRM:items=logout;n=2;title="退出当前账号？";detail="仅清除登录，保留网络";break;
        case NETEASE_APP_WIFI_CONFIRM:items=reset;n=2;title="重新连接 Wi-Fi？";detail="清除网络，保留网易云登录";break;
        case NETEASE_APP_ENDED:items=ended;n=2;break;
        case NETEASE_APP_ABOUT:items=NULL;n=0;title="余白 / FOLO";
#ifdef CONFIG_NETEASE_DIRECT
            detail="开源协议直连";
#else
            detail="中转服务";
#endif
            break;
        default:break;
        }
        if(s_logic.state==NETEASE_APP_BRIGHTNESS)device_music_ui_draw_brightness(screen,s_brightness);
        else if(s_logic.state==NETEASE_APP_NAVIGATION)device_music_ui_draw_menu(screen,title,detail,items,n,s_logic.selected);
        else {
            device_music_ui_preference_t page=DEVICE_MUSIC_PREF_SETTINGS;
            switch(s_logic.state) {
            case NETEASE_APP_APPEARANCE:page=DEVICE_MUSIC_PREF_APPEARANCE;break;
            case NETEASE_APP_NETWORK:page=DEVICE_MUSIC_PREF_NETWORK;break;
            case NETEASE_APP_ACCOUNT:page=DEVICE_MUSIC_PREF_ACCOUNT;break;
            case NETEASE_APP_LOGOUT_CONFIRM:page=DEVICE_MUSIC_PREF_LOGOUT;break;
            case NETEASE_APP_WIFI_CONFIRM:page=DEVICE_MUSIC_PREF_RESET_WIFI;break;
            case NETEASE_APP_ABOUT:page=DEVICE_MUSIC_PREF_ABOUT;break;
            case NETEASE_APP_ENDED:page=DEVICE_MUSIC_PREF_END;break;
            default:break;
            }
            device_music_ui_draw_preferences(screen,page,detail,esp_app_get_description()->version,items,n,s_logic.selected);
        }
    } else if(s_logic.state==NETEASE_APP_SIGNED_IN && screen_id!=DEVICE_MUSIC_SCREEN_LOADING_TRACK) {
        device_music_ui_draw_notice(screen,DEVICE_MUSIC_NOTICE_SIGNED_IN,s_nickname);
    } else switch (screen_id) {
    case DEVICE_MUSIC_SCREEN_LOADING_TRACK: {
        netease_work_t loading=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
        const char *title=(loading==NETEASE_WORK_LOAD_TRACKS || loading==NETEASE_WORK_NEXT_PLAYING_PAGE)?s_track_heading:loading==NETEASE_WORK_LOAD_TRACK_URL?"正在准备播放":"接着上次，听。";
        if(s_library_loading || loading==NETEASE_WORK_LOAD_LIBRARY || loading==NETEASE_WORK_START_QR || loading==NETEASE_WORK_SWITCH_ACCOUNT)
            device_music_ui_draw_notice(screen,(s_library_loading || loading==NETEASE_WORK_LOAD_LIBRARY)?DEVICE_MUSIC_NOTICE_LIBRARY:DEVICE_MUSIC_NOTICE_RESTORE,NULL);
        else device_music_ui_draw_progress(screen,screen_id,title,loading==NETEASE_WORK_LOAD_TRACKS?"读取歌曲编号\n随后获取歌名与歌手":loading==NETEASE_WORK_LOAD_TRACK_URL?"获取音频地址\n随后连接音频流":"正在验证已保存的登录状态",1);
        break;
    }
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
    case DEVICE_MUSIC_SCREEN_PLAY_FAILED: {
        device_music_ui_notice_t notice=s_session_expired?DEVICE_MUSIC_NOTICE_SESSION:s_qr_expired?DEVICE_MUSIC_NOTICE_EXPIRED:
            !s_sta_got_ip?DEVICE_MUSIC_NOTICE_OFFLINE:
            s_retry_work==NETEASE_WORK_LOAD_TRACK_URL?DEVICE_MUSIC_NOTICE_PLAY_FAILED:DEVICE_MUSIC_NOTICE_LOAD_FAILED;
#ifdef CONFIG_NETEASE_DIRECT
        if(s_error==NETEASE_DIRECT_ERR_UNAVAILABLE)notice=DEVICE_MUSIC_NOTICE_UNAVAILABLE;
#endif
        device_music_ui_draw_notice(screen,notice,s_error==ESP_ERR_NO_MEM?"设备内存不足\nOK 重试，长按返回":NULL);
        break;
    }
    default:
        device_music_ui_draw_message(screen, screen_id, NULL, NULL);
        break;
    }

    if(screen_id==DEVICE_MUSIC_SCREEN_LOGIN_WAITING)device_music_ui_set_footer(screen,"—","等待确认","—");
    if(screen_id==DEVICE_MUSIC_SCREEN_LOADING_TRACK)device_music_ui_set_footer(screen,"—","等待","—");
    if(screen_id==DEVICE_MUSIC_SCREEN_TRACKS && !s_tracks.count && s_logic.state==NETEASE_APP_TRACKS)device_music_ui_set_footer(screen,"—","返回歌单","—");
    if(screen_id==DEVICE_MUSIC_SCREEN_PLAYLISTS && !s_playlists.count)device_music_ui_set_footer(screen,"—","刷新","—");
    if (netease_app_logic_volume_overlay(&s_logic)) {
        memset(&s_player_widgets,0,sizeof(s_player_widgets));
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
    s_rendered_connected=s_sta_got_ip;s_rendered_signal_level=s_signal_level;s_rendered_soc=bsp_battery_soc();
    s_rendered_state=s_logic.state;
    s_rendered_night=device_music_ui_is_night();
    s_rendered_screen = screen_id;
    s_rendered_selected = netease_app_logic_selected(&s_logic);
    s_rendered_playing = netease_app_logic_is_playing(&s_logic);
    s_rendered_buffering=s_audio_started && s_logic.playing && audio_stream_elapsed_ms()==0;
    s_rendered_volume_overlay = netease_app_logic_volume_overlay(&s_logic);
    s_rendered_volume_percent = audio_stream_volume();
    s_rendered_elapsed_seconds = audio_stream_elapsed_ms() / 1000U;
    copy_limited(s_rendered_message, sizeof(s_rendered_message), s_message);
    copy_limited(s_rendered_qr_payload, sizeof(s_rendered_qr_payload), s_qr_payload);
}

static void request_playing_page(uint32_t offset,bool last)
{
    int64_t browse_playlist=s_track_playlist_id;uint32_t browse_offset=s_track_offset;
    char heading[NETEASE_CLIENT_NAME_LEN];copy_limited(heading,sizeof(heading),s_track_heading);
    s_page_last=last;s_track_playlist_id=s_playing_playlist_id;s_track_offset=offset;
    copy_limited(s_track_heading,sizeof(s_track_heading),s_playing_heading);
    request_work(NETEASE_WORK_NEXT_PLAYING_PAGE);
    s_track_playlist_id=browse_playlist;s_track_offset=browse_offset;
    copy_limited(s_track_heading,sizeof(s_track_heading),heading);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    int64_t now=esp_timer_get_time();
    if(!s_sta_got_ip) { s_signal_level=0;s_signal_checked_us=0; }
    else if(!s_signal_checked_us || now-s_signal_checked_us>=5000000) {
        wifi_ap_record_t ap={0};
        bool valid=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
        s_signal_level=device_music_ui_signal_level(true,valid,ap.rssi);
        s_signal_checked_us=now;
    }
    if(s_audio_started && !s_worker_busy && s_work==NETEASE_WORK_NONE) {
        audio_stream_state_t audio=audio_stream_state();
        if(audio==AUDIO_STREAM_ERROR) {
            s_audio_started=false;s_logic.playing=false;s_retry_work=NETEASE_WORK_LOAD_TRACK_URL;
            s_error=ESP_FAIL;s_url_tracks=s_playing_tracks;s_url_index=s_logic.playing_index;
            s_request_playlist=s_playing_playlist_id;s_request_offset=s_playing_offset;
            copy_limited(s_request_heading,sizeof(s_request_heading),s_playing_heading);
            if(s_logic.state==NETEASE_APP_PLAYING)netease_app_logic_set_error(&s_logic);
            else s_background_error=ESP_FAIL;
            copy_limited(s_message,sizeof(s_message),"Audio stream failed");
        } else if(audio==AUDIO_STREAM_STOPPED) {
            s_audio_started=false;s_logic.playing=false;
            netease_app_state_t previous=s_logic.state;size_t selected=s_logic.selected,count=s_logic.item_count;
            if(s_logic.playing_index+1<s_playing_tracks.count) {
                s_logic.state=NETEASE_APP_PLAYING;s_logic.selected=s_logic.playing_index+1;
                request_work(NETEASE_WORK_LOAD_TRACK_URL);
            } else if(s_playing_tracks.has_next_offset) {
                request_playing_page((uint32_t)s_playing_tracks.next_offset,false);
            } else {
                s_playback_ended=true;
                if(previous==NETEASE_APP_PLAYING) { s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2; }
            }
            if(previous!=NETEASE_APP_PLAYING && s_work!=NETEASE_WORK_NONE) {
                s_detached_work=true;s_logic.state=previous;s_logic.selected=selected;s_logic.item_count=count;
            }
        }
    }
    maybe_prefetch_tracks();
    render_netease_ui();
}

void netease_app_enter(void)
{
    if (s_worker_task) {
        ESP_LOGW(TAG, "Previous page worker is still stopping");
        return;
    }
    memset(&s_track_cache, 0, sizeof(s_track_cache));
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
    netease_app_logic_init(&s_logic);
    s_retry_work=NETEASE_WORK_NONE;
    s_active_work=NETEASE_WORK_NONE;
    s_display_loaded=false;s_display_dirty=false;s_detached_work=false;s_audio_started=false;s_playback_ended=false;
    s_prefetch_retry_us=0;
    s_library_loading=false;s_session_expired=false;s_qr_expired=false;s_qr_refresh_requested=false;s_browse_last=false;s_request_cursor=0;
    s_nickname[0]='\0';s_background_error=ESP_OK;s_rendered_soc=-2;
    memset(&s_playing_tracks,0,sizeof(s_playing_tracks));memset(&s_now_playing,0,sizeof(s_now_playing));

    render_netease_ui();
    s_timer = lv_timer_create(tick, 250, NULL);

    if (s_worker_task == NULL) {
        if (xTaskCreate(worker_task, "netease_app", 7168, NULL, 4, &s_worker_task) != pdPASS) {
            s_error = ESP_ERR_NO_MEM;
            netease_app_logic_set_error(&s_logic);
            copy_limited(s_message, sizeof(s_message), "Not enough memory\nRestart to retry");
            render_netease_ui();
            return;
        }
    }
    request_work(NETEASE_WORK_START_QR);
}

void netease_app_exit(void)
{
    s_exit_requested = true;
    audio_stream_stop();
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if(s_worker_task)xTaskNotifyGive(s_worker_task);
    /* The worker may be finishing TLS. It checks exit before every UI publish;
       never wait for its UI mutex while the caller owns that mutex. */
    if(s_worker_task==NULL)stop_wifi();
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
    if(s_track_playlist_id==s_playlists.items[index].id && s_tracks.count) {
        s_request_cursor=s_logic.track_cursor;return;
    }
    s_logic.track_cursor=0;s_browse_last=false;
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
        uint32_t previous_offset=s_track_offset;
        s_logic.track_cursor=0;s_browse_last=false;
        s_track_offset = (uint32_t)s_tracks.next_offset;
        copy_limited(s_message, sizeof(s_message), "Loading next songs...");
        request_work(NETEASE_WORK_LOAD_TRACKS);
        if(s_work==NETEASE_WORK_LOAD_TRACKS)s_track_offset=previous_offset;
        render_netease_ui();
        return true;
    }
    if (button == NETEASE_APP_BTN_UP && selected == 0 && s_track_offset > 0) {
        uint32_t previous_offset=s_track_offset;
        s_browse_last=true;
        s_track_offset = s_track_offset > NETEASE_APP_TRACK_PAGE_SIZE ?
                         s_track_offset - NETEASE_APP_TRACK_PAGE_SIZE :
                         0;
        copy_limited(s_message, sizeof(s_message), "Loading previous songs...");
        request_work(NETEASE_WORK_LOAD_TRACKS);
        if(s_work==NETEASE_WORK_LOAD_TRACKS)s_track_offset=previous_offset;
        render_netease_ui();
        return true;
    }
    return false;
}

void netease_app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if ((int)btn < 0 || (int)btn >= NETEASE_APP_BUTTON_COUNT) return;

    /* The BSP emits SINGLE_CLICK only for a short press, never after LONG. */
    if (ev == BSP_BTN_CLICK) {
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
    bool work_pending = (s_worker_busy && s_active_work!=NETEASE_WORK_PREFETCH_TRACKS) || s_work != NETEASE_WORK_NONE;
    if(work_pending) {
        netease_work_t active=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
        if((active==NETEASE_WORK_START_QR || active==NETEASE_WORK_SWITCH_ACCOUNT) && s_logic.state==NETEASE_APP_LOGIN_QR && s_qr_payload[0] && button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK) {
            s_qr_refresh_requested=true;s_qr_payload[0]='\0';render_netease_ui();return;
        }
        if(active!=NETEASE_WORK_LOAD_LIBRARY && active!=NETEASE_WORK_LOAD_TRACKS && active!=NETEASE_WORK_LOAD_TRACK_URL && active!=NETEASE_WORK_NEXT_PLAYING_PAGE)return;
        if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_LONG)s_detached_work=true;
        if(!s_detached_work)return;
        netease_app_logic_t candidate=s_logic;
        netease_app_action_t pending_action=netease_app_logic_on_button(&candidate,button,event);
        if(pending_action!=NETEASE_APP_ACTION_NONE || candidate.volume_overlay || candidate.state==NETEASE_APP_TRACKS)return;
        s_logic=candidate;render_netease_ui();return;
    }

    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.state==NETEASE_APP_SIGNED_IN) {
        request_work(NETEASE_WORK_LOAD_LIBRARY);render_netease_ui();return;
    }
#ifdef CONFIG_NETEASE_DIRECT
    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.state==NETEASE_APP_ERROR && s_error==NETEASE_DIRECT_ERR_UNAVAILABLE) {
        if(s_url_index+1<s_url_tracks.count) {
            ++s_url_index;s_detached_work=false;s_work=NETEASE_WORK_LOAD_TRACK_URL;xTaskNotifyGive(s_worker_task);
        } else if(s_url_tracks.has_next_offset) {
            s_track_playlist_id=s_request_playlist;s_track_offset=(uint32_t)s_url_tracks.next_offset;
            copy_limited(s_track_heading,sizeof(s_track_heading),s_request_heading);
            s_page_last=false;request_work(NETEASE_WORK_NEXT_PLAYING_PAGE);
        } else {
            s_tracks=s_url_tracks;s_track_playlist_id=s_request_playlist;s_track_offset=s_request_offset;
            copy_limited(s_track_heading,sizeof(s_track_heading),s_request_heading);
            s_logic.track_cursor=s_url_index;netease_app_logic_set_tracks(&s_logic,s_tracks.count);
        }
        render_netease_ui();return;
    }
#endif
    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.state==NETEASE_APP_ERROR && s_session_expired) {
        request_work(NETEASE_WORK_SWITCH_ACCOUNT);render_netease_ui();return;
    }
    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.item_count==0) {
        if(s_logic.state==NETEASE_APP_PLAYLISTS) { request_work(NETEASE_WORK_LOAD_LIBRARY);render_netease_ui();return; }
        if(s_logic.state==NETEASE_APP_TRACKS) { netease_app_logic_set_playlists(&s_logic,s_playlists.count);render_netease_ui();return; }
    }
    if(s_logic.state==NETEASE_APP_PLAYING && !s_logic.volume_overlay && event==NETEASE_APP_BTN_CLICK &&
       ((button==NETEASE_APP_BTN_DOWN && s_logic.selected+1>=s_playing_tracks.count && s_playing_tracks.has_next_offset) ||
        (button==NETEASE_APP_BTN_UP && s_logic.selected==0 && s_playing_offset>0))) {
        uint32_t offset=button==NETEASE_APP_BTN_DOWN?(uint32_t)s_playing_tracks.next_offset:
            (s_playing_offset>=NETEASE_APP_TRACK_PAGE_SIZE?s_playing_offset-NETEASE_APP_TRACK_PAGE_SIZE:0);
        request_playing_page(offset,button==NETEASE_APP_BTN_UP);render_netease_ui();return;
    }
    if (try_request_track_page(button, event)) {
        return;
    }

    netease_app_state_t previous_state=s_logic.state;
    size_t previous_selected=s_logic.selected;
    netease_app_action_t action = netease_app_logic_dispatch_button(&s_logic, button, event, false);
    if(previous_state==NETEASE_APP_NAVIGATION && previous_selected==0 && s_background_error!=ESP_OK && s_logic.state==NETEASE_APP_PLAYING) {
        netease_app_logic_set_error(&s_logic);
    }
    if(previous_state==NETEASE_APP_NAVIGATION && previous_selected==0 && s_playback_ended && s_logic.state==NETEASE_APP_PLAYING) {
        s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2;
    }
    if(((previous_state==NETEASE_APP_NAVIGATION && previous_selected==1) || previous_state==NETEASE_APP_ENDED) && s_logic.has_player && button==NETEASE_APP_BTN_OK && s_logic.state==NETEASE_APP_TRACKS) {
        s_tracks=s_playing_tracks;s_track_playlist_id=s_playing_playlist_id;s_track_offset=s_playing_offset;
        copy_limited(s_track_heading,sizeof(s_track_heading),s_playing_heading);
        s_logic.track_cursor=s_logic.playing_index;netease_app_logic_set_tracks(&s_logic,s_tracks.count);
    }
    if (action == NETEASE_APP_ACTION_RESET_CLOUD_SETUP) {
        request_work(NETEASE_WORK_RESET_WIFI);return;
    }
    if (action == NETEASE_APP_ACTION_SET_DAY || action == NETEASE_APP_ACTION_SET_NIGHT) {
        device_music_ui_set_night(action==NETEASE_APP_ACTION_SET_NIGHT);
        request_work(NETEASE_WORK_SAVE_DISPLAY);
    } else if (action == NETEASE_APP_ACTION_BRIGHTNESS_UP || action == NETEASE_APP_ACTION_BRIGHTNESS_DOWN) {
        s_brightness += action==NETEASE_APP_ACTION_BRIGHTNESS_UP?5:-5;
        if(s_brightness<10)s_brightness=10;
        if(s_brightness>100)s_brightness=100;
        bsp_display_backlight(s_brightness);s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;
        request_work(NETEASE_WORK_SAVE_DISPLAY);
    } else if (action == NETEASE_APP_ACTION_RETRY) {
        size_t failed_index=s_url_index;
        netease_work_t retry=s_retry_work;s_background_error=ESP_OK;
        if(retry==NETEASE_WORK_LOAD_TRACK_URL) {
            s_detached_work=false;s_work=retry;xTaskNotifyGive(s_worker_task);
        } else if(retry==NETEASE_WORK_LOAD_TRACKS || retry==NETEASE_WORK_NEXT_PLAYING_PAGE) {
            s_detached_work=false;s_work=retry;xTaskNotifyGive(s_worker_task);
        } else request_work(s_retry_work==NETEASE_WORK_NONE?NETEASE_WORK_START_QR:s_retry_work);
        if(s_retry_work==NETEASE_WORK_LOAD_TRACK_URL)s_url_index=failed_index;
    }
    if (action == NETEASE_APP_ACTION_START_QR) {
        request_work(NETEASE_WORK_START_QR);
    } else if (action == NETEASE_APP_ACTION_LOAD_TRACKS) {
        prepare_track_load_from_selected_playlist();
        request_work(NETEASE_WORK_LOAD_TRACKS);
    } else if (action == NETEASE_APP_ACTION_LOAD_TRACK_URL) {
        s_browse_last=false;
        if(previous_state==NETEASE_APP_ENDED && s_playing_offset>0)request_playing_page(0,false);
        else request_work(NETEASE_WORK_LOAD_TRACK_URL);
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
