#include "netease_app.h"

#include "audio_stream.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "device_music_ui.h"
#include "display_settings.h"
#include "music_lyrics.h"
#include "music_playback_order.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "demo_radio.h"
#include "netease_app_logic.h"
#include "netease_client.h"
#ifdef CONFIG_NETEASE_DIRECT
#include "netease_direct_adapter.h"
#include "music_artwork.h"
#include "music_provider.h"
#include "music_qr_image.h"
#include "qqmusic_login.h"
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
    NETEASE_WORK_ORDER_STEP,
    NETEASE_WORK_SAVE_MODE,
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
static volatile bool s_wifi_reconnect_pending;
/* Retry timing is owned by the worker; event callbacks only publish flags. */
static unsigned s_wifi_reconnect_attempt;
static int64_t s_wifi_reconnect_due_us;
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
static netease_app_state_t s_rendered_state;
static bool s_rendered_night;
static bool s_display_dirty;
static netease_work_t s_active_work;
static bool s_detached_work;
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
static track_cache_t *s_track_cache;
/* UI mutex held, or before worker startup. Browsing cache yields RAM to TLS. */
static void release_track_cache(void)
{
    free(s_track_cache);s_track_cache=NULL;
}
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
static void copy_limited(char *dst, size_t dst_len, const char *src);
static music_playback_order_t s_order, s_candidate_order;
static bool s_candidate_valid, s_order_selection_valid;
static music_playback_selection_t s_order_selection;
static music_playback_direction_t s_order_direction;
static music_playback_mode_t s_mode, s_requested_mode;
static char s_mode_status[96];
static bool s_order_load_prepared;
static bool s_skip_unavailable;
static bool s_mode_loaded;
#ifdef CONFIG_NETEASE_DIRECT
static bool s_platform_selected;
static volatile int s_platform_choice=-1;
static volatile bool s_platform_return_requested;
static lv_image_dsc_t s_qq_qr_image;
#endif
static bool request_is_current(void)
{
#ifdef CONFIG_NETEASE_DIRECT
    return !s_exit_requested && !s_platform_return_requested;
#else
    return !s_exit_requested;
#endif
}

static bool playback_random(void *ctx, uint32_t *value)
{
    (void)ctx;*value=esp_random();return true;
}
static void init_playback_order(music_playback_order_t *order)
{
    const music_playback_config_t config={.rng=playback_random};
    music_playback_order_init(order,&config);
}
/* 留出音频、TLS等共存空间；这是保守预算，支持上限须由实机测量确认。 */
static size_t playback_queue_budget(void)
{
    size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    return netease_app_playback_budget(free_bytes,largest,audio_stream_is_active());
}
static esp_err_t bind_playback_order(music_playback_order_t *order, music_playback_mode_t mode,
                                    const netease_track_list_t *tracks,uint32_t current)
{
    if(!tracks->metadata_valid)return ESP_ERR_INVALID_RESPONSE;
    char identity[128];
#ifdef CONFIG_NETEASE_DIRECT
    const char *account=music_provider_account();
#else
    const char *account="0";
#endif
    snprintf(identity,sizeof(identity),"%s/%s/%" PRId64 "/%" PRIu64,
#ifdef CONFIG_NETEASE_DIRECT
             music_provider_platform()==MUSIC_PLATFORM_QQ?"qqmusic":"netease",
#else
             "netease",
#endif
             account,tracks->playlist_id,tracks->revision);
    size_t budget=playback_queue_budget();
    music_playback_result_t result=music_playback_order_bind(order,mode,identity,
        tracks->total_count,current,budget);
    if(result==MUSIC_PLAYBACK_OK)return ESP_OK;
    ESP_LOGW(TAG,"Playback queue rejected: mode=%d total=%" PRIu32 " budget=%u result=%d",
             (int)mode,tracks->total_count,(unsigned)budget,(int)result);
    return result==MUSIC_PLAYBACK_NO_MEMORY || result==MUSIC_PLAYBACK_BUDGET?ESP_ERR_NO_MEM:ESP_ERR_INVALID_STATE;
}
static void accept_candidate_order(void)
{
    if(!s_candidate_valid)return;
    music_playback_order_clear(&s_order);
    s_order=s_candidate_order;
    init_playback_order(&s_candidate_order);
    s_candidate_valid=false;
}
static esp_err_t load_playback_mode(void)
{
    nvs_handle_t nvs;uint8_t mode=0;
    esp_err_t err=nvs_open("music_playback",NVS_READONLY,&nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    err=nvs_get_u8(nvs,"mode",&mode);nvs_close(nvs);
    if(err==ESP_ERR_NVS_NOT_FOUND)return ESP_OK;
    if(err!=ESP_OK)return err;
    if(mode>1)return ESP_ERR_INVALID_RESPONSE;
    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    s_mode=(music_playback_mode_t)mode;s_logic.playback_mode=mode;
    bsp_lvgl_unlock();
    return ESP_OK;
}
static esp_err_t save_playback_mode(void)
{
    if(s_requested_mode==s_mode) {
        ESP_LOGI(TAG,"Playback mode unchanged: mode=%d",(int)s_mode);
        if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
        copy_limited(s_mode_status,sizeof(s_mode_status),"当前播放模式未改变");
        s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;bsp_lvgl_unlock();return ESP_OK;
    }
    music_playback_order_clear(&s_candidate_order);s_candidate_valid=false;
    s_order_load_prepared=false;
    esp_err_t err=ESP_OK;
    if(s_logic.has_player) {
        err=bind_playback_order(&s_candidate_order,s_requested_mode,&s_playing_tracks,s_playing_number-1);
        s_candidate_valid=err==ESP_OK;
    }
    nvs_handle_t nvs;
    if(err==ESP_OK) {
        err=nvs_open("music_playback",NVS_READWRITE,&nvs);
        if(err==ESP_OK) {
            err=nvs_set_u8(nvs,"mode",(uint8_t)s_requested_mode);
            if(err==ESP_OK)err=nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    if(err==ESP_OK) {
        accept_candidate_order();s_mode=s_requested_mode;s_logic.playback_mode=s_mode;
        s_order_load_prepared=false;s_order_selection_valid=false;
        copy_limited(s_mode_status,sizeof(s_mode_status),"播放模式已保存");
        ESP_LOGI(TAG,"Playback mode saved: mode=%d total=%" PRIu32 " queue_bytes=%u audio_active=%d",
                 (int)s_mode,s_order.total,(unsigned)s_order.allocated_bytes,audio_stream_is_active());
    } else {
        music_playback_order_clear(&s_candidate_order);s_candidate_valid=false;
        copy_limited(s_mode_status,sizeof(s_mode_status),err==ESP_ERR_NO_MEM?
                     "内存不足，模式未更改":"保存失败，模式未更改");
    }
    s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;bsp_lvgl_unlock();
    return err;
}

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
    case NETEASE_WORK_ORDER_STEP:return "order_step";
    case NETEASE_WORK_SAVE_MODE:return "save_mode";
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
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_got_ip = false;
        s_wifi_reconnect_pending = true;
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Playback Wi-Fi disconnected: reason=%u", event ? (unsigned)event->reason : 0);
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        s_sta_got_ip = true;
        s_wifi_reconnect_pending = false;
        ESP_LOGI(TAG, "Playback Wi-Fi received IP");
    }
}

static void maintain_wifi(void)
{
    if (!s_wifi_started || s_exit_requested) return;
    if (s_sta_got_ip) {
        s_wifi_reconnect_attempt = 0;
        s_wifi_reconnect_due_us = 0;
        return;
    }
    if (!s_wifi_reconnect_pending || s_wifi_reconnect_attempt >= 6 ||
        esp_timer_get_time() < s_wifi_reconnect_due_us) return;
    unsigned shift = s_wifi_reconnect_attempt < 4 ? s_wifi_reconnect_attempt : 4;
    unsigned delay_ms = 500U << shift;
    ++s_wifi_reconnect_attempt;
    s_wifi_reconnect_due_us = esp_timer_get_time() + (int64_t)delay_ms * 1000;
    /* Avoid repeated connect calls while the driver is already connecting. */
    s_wifi_reconnect_pending = false;
    esp_err_t err = esp_wifi_connect();
    ESP_LOGI(TAG, "Playback Wi-Fi reconnect: attempt=%u result=%s next_ms=%u",
             s_wifi_reconnect_attempt, esp_err_to_name(err), delay_ms);
    if (err != ESP_OK) s_wifi_reconnect_pending = true;
    if (s_wifi_reconnect_attempt == 6)
        ESP_LOGW(TAG, "Wi-Fi retry budget used; final attempt may still connect, manual retry remains available");
}

static esp_err_t start_wifi(void)
{
    if (s_wifi_started) {
        if(s_sta_got_ip)return ESP_OK;
        s_wifi_reconnect_attempt = 0;
        s_wifi_reconnect_due_us = 0;
        s_wifi_reconnect_pending = true;
        for(int i=0;i<200 && !s_exit_requested;i++) {
            maintain_wifi();
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
    /* Provisioning deinitializes Wi-Fi; configure the playback driver too. */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;

    s_sta_got_ip = false;
    s_wifi_reconnect_attempt = 0;
    s_wifi_reconnect_due_us = 0;
    s_wifi_reconnect_pending = true;
    for (int i = 0; i < 200; i++) {
        if (s_exit_requested) return ESP_ERR_INVALID_STATE;
        maintain_wifi();
        if (s_sta_got_ip) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t sync_time_if_possible(void)
{
    time_t now = time(NULL);
    if(!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
    if (now > DISPLAY_VALID_CLOCK_EPOCH) return ESP_OK;

    for (int i = 0; i < 100; i++) {
        now = time(NULL);
        if (now > DISPLAY_VALID_CLOCK_EPOCH) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static void stop_wifi(void)
{
    s_wifi_reconnect_pending = false;
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
    s_sta_got_ip = false;
    s_wifi_reconnect_pending = false;
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
    return ESP_OK;
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
    esp_err_t err = music_provider_playlists(loaded);
#else
    netease_playlist_list_t *loaded=calloc(1,sizeof(*loaded));
    if(!loaded)return ESP_ERR_NO_MEM;
    esp_err_t err = netease_client_get_playlists(&s_client,loaded);
#endif
    if (err == ESP_OK) {
        if(bsp_lvgl_lock(0)) {
            if(request_is_current()) {
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

#ifdef CONFIG_NETEASE_DIRECT
static bool login_commit_acquire(void *ctx)
{
    (void)ctx;
    if(!bsp_lvgl_lock(1000))return false;
    if(!request_is_current() || s_qr_refresh_requested) { bsp_lvgl_unlock();return false; }
    return true;
}
static void login_commit_release(void *ctx) { (void)ctx;bsp_lvgl_unlock(); }
static esp_err_t choose_platform(void)
{
    if(s_platform_selected && !s_platform_return_requested)return ESP_OK;
    audio_stream_stop();music_provider_close();
    music_playback_order_clear(&s_order);music_playback_order_clear(&s_candidate_order);
    if(!bsp_lvgl_lock(1000))return ESP_ERR_TIMEOUT;
    s_platform_return_requested=false;s_platform_selected=false;s_platform_choice=-1;
    s_candidate_valid=false;s_order_selection_valid=false;s_order_load_prepared=false;s_skip_unavailable=false;
    s_audio_started=false;s_playback_ended=false;s_background_error=ESP_OK;s_detached_work=false;
    release_track_cache();memset(&s_playlists,0,sizeof(s_playlists));
    memset(&s_tracks,0,sizeof(s_tracks));memset(&s_playing_tracks,0,sizeof(s_playing_tracks));
    memset(&s_url_tracks,0,sizeof(s_url_tracks));
    memset(&s_now_playing,0,sizeof(s_now_playing));memset(&s_track_url,0,sizeof(s_track_url));
    s_track_playlist_id=0;s_playing_playlist_id=0;s_track_offset=0;s_playing_offset=0;s_prefetch_playlist=0;
    s_nickname[0]=0;s_qr_payload[0]=0;s_qr_refresh_requested=false;s_session_expired=false;s_qr_expired=false;
    device_music_ui_set_media(NULL,NULL);device_music_ui_reset_screen(s_scr,DEVICE_MUSIC_SCREEN_SETUP);
    music_qr_image_clear(&s_qq_qr_image);lv_image_cache_drop(&s_cover_image);
    free((void *)s_cover_image.data);memset(&s_cover_image,0,sizeof(s_cover_image));
    free(s_lyric_text);s_lyric_text=NULL;s_lyric_status=NULL;s_current_lyric[0]=0;
    netease_app_logic_init(&s_logic);s_logic.playback_mode=s_mode;
    s_logic.timeout_option=display_timeout_option(display_settings_timeout());
    s_logic.state=NETEASE_APP_PLATFORM;s_logic.item_count=2;
    bsp_lvgl_unlock();
    nvs_handle_t store;uint8_t last=0;
    esp_err_t saved=nvs_open("music_platform",NVS_READONLY,&store);
    if(saved==ESP_OK) { saved=nvs_get_u8(store,"last",&last);nvs_close(store); }
    if(saved!=ESP_OK && saved!=ESP_ERR_NVS_NOT_FOUND)return saved;
    if(last>1)return ESP_ERR_INVALID_RESPONSE;
    if(bsp_lvgl_lock(1000)) { s_logic.selected=last;copy_limited(s_message,sizeof(s_message),"请选择音乐平台");render_netease_ui();bsp_lvgl_unlock(); }
    ESP_LOGI(TAG,"Platform selection ready; connected=%d",s_sta_got_ip);
    while(!s_exit_requested && s_platform_choice<0) { maintain_wifi();vTaskDelay(pdMS_TO_TICKS(100)); }
    if(s_exit_requested)return ESP_ERR_INVALID_STATE;
    if(!s_sta_got_ip) { esp_err_t err=start_wifi();if(err!=ESP_OK)return err; }
    esp_err_t err=music_provider_open((music_platform_t)s_platform_choice);
    if(err==ESP_OK) {
        err=nvs_open("music_platform",NVS_READWRITE,&store);
        if(err==ESP_OK) { err=nvs_set_u8(store,"last",(uint8_t)s_platform_choice);if(err==ESP_OK)err=nvs_commit(store);nvs_close(store); }
    }
    if(err==ESP_OK && bsp_lvgl_lock(1000)) {
        s_platform_selected=true;s_logic.state=NETEASE_APP_LOGIN_QR;s_logic.selected=0;s_logic.item_count=0;
        copy_limited(s_message,sizeof(s_message),"正在检查所选平台登录状态");bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG,"Platform selected: %s",music_provider_name());
    return err;
}
static esp_err_t run_qq_login(void)
{
    qqmusic_login_qr_t *qr=calloc(1,sizeof(*qr));if(!qr)return ESP_ERR_NO_MEM;
    qqmusic_login_connection_t *connection=NULL;lv_image_dsc_t image={0};
    esp_err_t err=qqmusic_login_create_qr(qr);
    int64_t deadline=esp_timer_get_time()+(int64_t)qr->expires_seconds*1000000;
    if(err==ESP_OK && !s_platform_return_requested && !s_exit_requested)err=qqmusic_login_connect(qr->id,&connection);
    int64_t ready_deadline=esp_timer_get_time()+15000000;
    while(err==ESP_OK && !s_exit_requested && !s_platform_return_requested && !s_qr_refresh_requested && !qqmusic_login_ready(connection)) {
        qqmusic_login_state_t state;char *credential=NULL;
        err=qqmusic_login_poll(connection,&state,&credential);
        if(credential) { volatile char *p=credential;size_t n=strlen(credential);while(n--)*p++=0;free(credential);err=ESP_ERR_INVALID_RESPONSE; }
        if(esp_timer_get_time()>ready_deadline && !qqmusic_login_ready(connection))err=ESP_ERR_TIMEOUT;
    }
    if(err==ESP_OK && !s_platform_return_requested && !s_exit_requested && !s_qr_refresh_requested)
        err=music_qr_image_decode(qr->png,qr->png_length,&image);
    memset(qr,0,sizeof(*qr));free(qr);
    if(err==ESP_OK && image.data && bsp_lvgl_lock(1000)) {
        if(!s_platform_return_requested && !s_exit_requested && !s_qr_refresh_requested) {
            music_qr_image_clear(&s_qq_qr_image);s_qq_qr_image=image;memset(&image,0,sizeof(image));
            strcpy(s_qr_payload,"qq-png");s_logic.state=NETEASE_APP_LOGIN_QR;
            copy_limited(s_message,sizeof(s_message),"使用 QQ 音乐 App 扫码并确认");render_netease_ui();
        }
        bsp_lvgl_unlock();
    }
    free((void *)image.data);
    bool confirmed=false;
    while(err==ESP_OK && !s_exit_requested && !s_platform_return_requested && !s_qr_refresh_requested && esp_timer_get_time()<deadline) {
        qqmusic_login_state_t state;char *credential=NULL;
        err=qqmusic_login_poll(connection,&state,&credential);
        if(err==ESP_OK && state==QQMUSIC_LOGIN_CONFIRMED && !s_exit_requested && !s_platform_return_requested && !s_qr_refresh_requested) {
            qqmusic_login_close(connection);connection=NULL;
            if(!bsp_lvgl_lock(1000))err=ESP_ERR_TIMEOUT;
            else {
                if(!s_exit_requested && !s_platform_return_requested && !s_qr_refresh_requested) {
                    err=qqmusic_client_accept(music_provider_qq_client(),credential);confirmed=err==ESP_OK;
                    s_qr_payload[0]=0;device_music_ui_reset_screen(s_scr,DEVICE_MUSIC_SCREEN_SETUP);music_qr_image_clear(&s_qq_qr_image);
                    if(confirmed)s_logic.state=NETEASE_APP_SIGNED_IN;
                    render_netease_ui();
                }
                bsp_lvgl_unlock();
            }
        }
        if(credential) { volatile char *p=credential;size_t n=strlen(credential);while(n--)*p++=0;free(credential); }
        if(err!=ESP_OK || confirmed)break;
        if(state==QQMUSIC_LOGIN_EXPIRED) { s_qr_expired=true;err=ESP_ERR_TIMEOUT;break; }
        if(state==QQMUSIC_LOGIN_REJECTED || state==QQMUSIC_LOGIN_CANCELED) { err=QQMUSIC_ERR_REJECTED;break; }
        if(state==QQMUSIC_LOGIN_SCANNED && bsp_lvgl_lock(1000)) {
            s_logic.state=NETEASE_APP_LOGIN_WAITING;copy_limited(s_message,sizeof(s_message),"已扫码，请在 QQ 音乐 App 确认");render_netease_ui();bsp_lvgl_unlock();
        }
    }
    qqmusic_login_close(connection);
    if(s_platform_return_requested || s_qr_refresh_requested || s_exit_requested)return ESP_OK;
    if(err==ESP_OK && !confirmed) { s_qr_expired=true;return ESP_ERR_TIMEOUT; }
    if(err==ESP_OK) { music_provider_nickname(s_nickname,sizeof(s_nickname));return load_playlists(); }
    return err;
}
#endif
static esp_err_t run_qr_login(void)
{
    if(!s_mode_loaded) {
        esp_err_t mode_err=load_playback_mode();
        if(mode_err!=ESP_OK)return mode_err;
        s_mode_loaded=true;
    }
    esp_err_t err = ensure_client_ready();
    if (err != ESP_OK) return err;
#ifdef CONFIG_NETEASE_DIRECT
    err=choose_platform();if(err!=ESP_OK)return err;
    err = music_provider_restore();
    if(err==NETEASE_DIRECT_ERR_AUTH) { s_session_expired=false;err=ESP_ERR_NOT_FOUND; }
    if(err==ESP_OK) { music_provider_nickname(s_nickname,sizeof(s_nickname));return load_playlists(); }
    if (err != ESP_ERR_NOT_FOUND) return err;
    if(music_provider_platform()==MUSIC_PLATFORM_QQ)return run_qq_login();
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
#ifdef CONFIG_NETEASE_DIRECT
        if(s_platform_return_requested)return ESP_OK;
#endif
        if(s_qr_refresh_requested)return ESP_OK;
        netease_qr_status_t status = {0};
#ifdef CONFIG_NETEASE_DIRECT
        netease_direct_set_session_guard(login_commit_acquire,login_commit_release,NULL);
        err = direct_adapter_qr_poll(qr.login_id, &status);
        netease_direct_set_session_guard(NULL,NULL,NULL);
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
            music_provider_nickname(s_nickname,sizeof(s_nickname));
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
    music_playback_order_clear(&s_order);music_playback_order_clear(&s_candidate_order);
    s_candidate_valid=false;s_order_selection_valid=false;s_order_load_prepared=false;
    s_audio_started=false;s_playback_ended=false;
    audio_stream_stop();
    copy_limited(s_message, sizeof(s_message), "Signing out...");
    refresh_ui_from_worker();

#ifdef CONFIG_NETEASE_DIRECT
    esp_err_t err = music_provider_logout();
#else
    esp_err_t err = netease_client_logout(&s_client);
#endif
    if (err != ESP_OK) return err;

    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    release_track_cache();
    memset(&s_playlists, 0, sizeof(s_playlists));
    memset(&s_tracks, 0, sizeof(s_tracks));
    memset(&s_track_url, 0, sizeof(s_track_url));
    s_qr_payload[0] = '\0';
    s_track_heading[0] = '\0';
    s_track_playlist_id = 0;
    s_track_offset = 0;
    netease_app_logic_init(&s_logic);s_session_expired=false;
    s_logic.playback_mode=s_mode;
    memset(&s_playing_tracks,0,sizeof(s_playing_tracks));memset(&s_now_playing,0,sizeof(s_now_playing));s_nickname[0]='\0';
    bsp_lvgl_unlock();
#ifdef CONFIG_NETEASE_DIRECT
    s_platform_return_requested=true;
#endif
    return run_qr_login();
}

static void publish_tracks(const netease_track_list_t *tracks)
{
    if(!bsp_lvgl_lock(0))return;
    if(s_exit_requested) { bsp_lvgl_unlock();return; }
    if(s_active_work==NETEASE_WORK_NEXT_PLAYING_PAGE || s_active_work==NETEASE_WORK_ORDER_STEP) { s_url_tracks=*tracks;bsp_lvgl_unlock();return; }
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
    for(size_t i=0;s_track_cache && i<3;i++) {
        if(s_track_cache[i].valid && s_track_cache[i].playlist==s_request_playlist && s_track_cache[i].offset==s_request_offset && esp_timer_get_time()-s_track_cache[i].loaded_us<300000000) {
            publish_tracks(&s_track_cache[i].tracks);
            ESP_LOGI(TAG,"Track page cache hit: offset=%" PRIu32,s_request_offset);return ESP_OK;
        }
    }
    if(s_request_playlist<0)return ESP_ERR_INVALID_ARG;
#ifdef CONFIG_NETEASE_DIRECT
    if(!s_request_playlist && music_provider_platform()!=MUSIC_PLATFORM_QQ)return ESP_ERR_INVALID_ARG;
#else
    if(!s_request_playlist)return ESP_ERR_INVALID_ARG;
#endif
    if(!s_sta_got_ip) { esp_err_t reconnect=start_wifi();if(reconnect!=ESP_OK)return reconnect; }
    copy_limited(s_message,sizeof(s_message),"Loading songs...");refresh_ui_from_worker();
    netease_track_list_t *page=calloc(1,sizeof(*page));if(!page)return ESP_ERR_NO_MEM;
    esp_err_t err=ESP_FAIL;
    for(unsigned attempt=0;attempt<3;attempt++) {
#ifdef CONFIG_NETEASE_DIRECT
        err=music_provider_tracks(s_request_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_request_offset,page);
#else
        err=netease_client_get_tracks(&s_client,s_request_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_request_offset,page);
#endif
        if(err==ESP_OK || (err!=ESP_ERR_NO_MEM && err!=ESP_ERR_HTTP_CONNECT && err!=ESP_ERR_TIMEOUT) || attempt==2)break;
        ESP_LOGW(TAG,"Track request retry: attempt=%u err=%s free=%u largest=%u",attempt+1,esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if(err==ESP_OK && request_is_current()) {
        if(bsp_lvgl_lock(1000)) {
            if(!s_track_cache && !audio_stream_is_active())s_track_cache=calloc(3,sizeof(*s_track_cache));
            if(s_track_cache) {
                track_cache_t *cache=&s_track_cache[s_cache_next++ % 3];
                cache->playlist=s_request_playlist;cache->offset=s_request_offset;cache->tracks=*page;cache->loaded_us=esp_timer_get_time();cache->valid=true;
            }
            bsp_lvgl_unlock();
        }
        publish_tracks(page);
        ESP_LOGI(TAG,"Tracks loaded: offset=%" PRIu32 " count=%u next=%s",s_request_offset,(unsigned)page->count,page->has_next_offset?"yes":"no");
    }
    free(page);
    return err;
}

/* Called with the UI mutex held: cached metadata is copied without I/O. */
static bool track_page_cached(int64_t playlist,uint32_t offset)
{
    for(size_t i=0;s_track_cache && i<3;i++)if(s_track_cache[i].valid && s_track_cache[i].playlist==playlist &&
        s_track_cache[i].offset==offset && esp_timer_get_time()-s_track_cache[i].loaded_us<300000000)return true;
    return false;
}
static void maybe_prefetch_tracks(void)
{
    /* Explicit browsing remains available; speculative TLS must not compete
     * with the bounded audio download reserve on a hotspot. */
    if (audio_stream_is_active()) return;
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
    netease_track_list_t *page=calloc(1,sizeof(*page));if(!page)return ESP_ERR_NO_MEM;
#ifdef CONFIG_NETEASE_DIRECT
    esp_err_t err=music_provider_tracks(s_prefetch_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_prefetch_offset,page);
#else
    esp_err_t err=netease_client_get_tracks(&s_client,s_prefetch_playlist,NETEASE_APP_TRACK_PAGE_SIZE,s_prefetch_offset,page);
#endif
    if(bsp_lvgl_lock(1000)) {
        if(err==ESP_OK && request_is_current() && !audio_stream_is_active()) {
            if(!s_track_cache)s_track_cache=calloc(3,sizeof(*s_track_cache));
            if(!s_track_cache) { bsp_lvgl_unlock();free(page);return ESP_ERR_NO_MEM; }
            /* Prefer evicting a page outside current / previous / next. */
            size_t slot=s_cache_next++%3;
            for(size_t i=0;i<3;i++)if(!s_track_cache[i].valid || s_track_cache[i].playlist!=s_track_playlist_id ||
                (s_track_cache[i].offset!=s_track_offset &&
                 (s_track_offset<NETEASE_APP_TRACK_PAGE_SIZE || s_track_cache[i].offset!=s_track_offset-NETEASE_APP_TRACK_PAGE_SIZE) &&
                 (!s_tracks.has_next_offset || s_track_cache[i].offset!=(uint32_t)s_tracks.next_offset))) { slot=i;break; }
            track_cache_t *cache=&s_track_cache[slot];
            cache->playlist=s_prefetch_playlist;cache->offset=s_prefetch_offset;cache->tracks=*page;
            cache->loaded_us=esp_timer_get_time();cache->valid=true;
            ESP_LOGI(TAG,"Prefetch ready: offset=%" PRIu32 " count=%u",s_prefetch_offset,(unsigned)page->count);
        } else if(err!=ESP_OK) {
            s_prefetch_retry_us=esp_timer_get_time()+30000000;
            ESP_LOGW(TAG,"Prefetch failed: offset=%" PRIu32 " err=%s; current page retained",s_prefetch_offset,esp_err_to_name(err));
        }
        bsp_lvgl_unlock();
    }
    free(page);
    return err;
}

static esp_err_t stop_audio_for_track_change(void)
{
    audio_stream_stop();
    if(audio_stream_is_active())return ESP_ERR_INVALID_STATE;
    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    s_logic.playing=false;s_audio_started=false;
    bsp_lvgl_unlock();return ESP_OK;
}

static esp_err_t load_track_url(void)
{
    size_t index=s_url_index;
    if(index>=s_url_tracks.count)return ESP_ERR_INVALID_ARG;
    if(!s_order_load_prepared) {
        music_playback_order_clear(&s_candidate_order);s_candidate_valid=false;
        if(s_url_tracks.metadata_valid) {
            esp_err_t prepared=bind_playback_order(&s_candidate_order,s_mode,&s_url_tracks,
                                                  s_request_offset+(uint32_t)index);
            if(prepared!=ESP_OK)return prepared;
            s_candidate_valid=true;
        } else if(s_mode==MUSIC_PLAYBACK_SHUFFLE)return ESP_ERR_INVALID_RESPONSE;
        s_order_selection_valid=false;s_order_load_prepared=true;
    }
    /* Release the old decoder and CDN connection before allocating API TLS. */
    esp_err_t stopped=stop_audio_for_track_change();if(stopped!=ESP_OK)return stopped;

    copy_limited(s_message, sizeof(s_message), "Loading play URL...");
    refresh_ui_from_worker();
    /* Detach old widgets under the UI lock before releasing their image data. */
    if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
    release_track_cache();
    device_music_ui_set_media(NULL,NULL);
    s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;
    lv_image_cache_drop(&s_cover_image);
    if(s_scr && request_is_current())render_netease_ui();
    free((void *)s_cover_image.data);memset(&s_cover_image,0,sizeof(s_cover_image));
    free(s_lyric_text);s_lyric_text=NULL;s_current_lyric[0]=0;s_lyric_status=NULL;
    bsp_lvgl_unlock();
#ifdef CONFIG_NETEASE_DIRECT
    ESP_LOGI(TAG, "Loading play URL: platform=%s position=%u mid=%s id=%" PRId64,
             music_provider_platform()==MUSIC_PLATFORM_QQ?"qqmusic":"netease",
             (unsigned)(s_request_offset+index+1),s_url_tracks.items[index].qq_mid,s_url_tracks.items[index].id);
    esp_err_t err = music_provider_url(&s_url_tracks.items[index], NETEASE_APP_PLAY_BITRATE, &s_track_url);
#else
    ESP_LOGI(TAG, "Loading play URL for track=%" PRId64, s_url_tracks.items[index].id);
    esp_err_t err = netease_client_get_track_url(&s_client, s_url_tracks.items[index].id, NETEASE_APP_PLAY_BITRATE, &s_track_url);
#endif
    if(err!=ESP_OK)return err;
#ifdef CONFIG_NETEASE_DIRECT
    lv_image_dsc_t cover={0};char cover_url[256];
    bool is_qq=music_provider_platform()==MUSIC_PLATFORM_QQ;
    esp_err_t cover_err=is_qq?ESP_ERR_NOT_FOUND:netease_direct_get_cover_url(s_url_tracks.items[index].id,cover_url,sizeof(cover_url));
    if(cover_err==ESP_OK && request_is_current())cover_err=music_artwork_load(cover_url,&cover);
    if(cover_err!=ESP_OK)ESP_LOGW(TAG,"Cover omitted: %s",esp_err_to_name(cover_err));
    char *lyrics=s_exit_requested || is_qq?NULL:malloc(8192);
    esp_err_t lyric_err=is_qq?ESP_ERR_NOT_FOUND:lyrics?netease_direct_get_lyrics(s_url_tracks.items[index].id,lyrics,8192):ESP_ERR_NO_MEM;
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
    if (err == ESP_OK && request_is_current()) {
#ifdef CONFIG_NETEASE_DIRECT
        /* The idle lyrics/API TLS connection competes with CDN RX and DMA. */
        netease_direct_client_release_connection();
#endif
        err = audio_stream_start(s_track_url.url);
        if (err == ESP_OK) {
            if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
            if(s_exit_requested) { bsp_lvgl_unlock();audio_stream_stop();return ESP_ERR_INVALID_STATE; }
            accept_candidate_order();
            if(s_order_selection_valid) {
                if(music_playback_order_commit(&s_order,&s_order_selection)!=MUSIC_PLAYBACK_OK) {
                    bsp_lvgl_unlock();audio_stream_stop();return ESP_ERR_INVALID_STATE;
                }
                s_order_selection_valid=false;
            }
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

/* 当前播放队列只由worker访问；UI仅提交方向，网络失败不消费队列。 */
static esp_err_t step_playback_order(void)
{
    if(s_skip_unavailable) {
        if(!s_order_load_prepared && !s_order_selection_valid) {
            esp_err_t err=bind_playback_order(&s_candidate_order,s_mode,&s_url_tracks,
                s_request_offset+(uint32_t)s_url_index);
            if(err!=ESP_OK)return err;
            s_candidate_valid=true;
        }
        accept_candidate_order();
        if(s_order_selection_valid) {
            if(music_playback_order_commit(&s_order,&s_order_selection)!=MUSIC_PLAYBACK_OK)return ESP_ERR_INVALID_STATE;
            s_order_selection_valid=false;
        }
        s_skip_unavailable=false;
    } else {
        music_playback_order_clear(&s_candidate_order);s_candidate_valid=false;
    }
    music_playback_result_t result=music_playback_order_peek(&s_order,s_order_direction,&s_order_selection);
    if(result==MUSIC_PLAYBACK_END) {
        if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
        /* 上一首到起点保留当前播放；下一首到末尾才停止。 */
        if(s_order_direction==MUSIC_PLAYBACK_NEXT) {
            bsp_lvgl_unlock();audio_stream_stop();
            if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
            s_audio_started=false;s_playback_ended=true;s_logic.playing=false;
            if(!s_detached_work) { s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2; }
        }
        bsp_lvgl_unlock();return ESP_OK;
    }
    if(result!=MUSIC_PLAYBACK_OK)return ESP_ERR_INVALID_STATE;
    s_order_selection_valid=true;
    uint32_t target=s_order_selection.index;
    /* Switching needs API TLS before the next audio TLS; release the old stream first. */
    esp_err_t err=stop_audio_for_track_change();if(err!=ESP_OK)return err;
    s_request_offset=(target/NETEASE_APP_TRACK_PAGE_SIZE)*NETEASE_APP_TRACK_PAGE_SIZE;
    err=load_tracks();
    if(err!=ESP_OK)return err;
    /* TTL/窗口刷新后内容变更：按真实新页重建本轮，不使用旧索引队列。 */
    if(!s_url_tracks.metadata_valid)return ESP_ERR_INVALID_RESPONSE;
    char expected[128];
#ifdef CONFIG_NETEASE_DIRECT
    const char *account=music_provider_account();
#else
    const char *account="0";
#endif
    snprintf(expected,sizeof(expected),"%s/%s/%" PRId64 "/%" PRIu64,
#ifdef CONFIG_NETEASE_DIRECT
             music_provider_platform()==MUSIC_PLATFORM_QQ?"qqmusic":"netease",
#else
             "netease",
#endif
             account,s_url_tracks.playlist_id,s_url_tracks.revision);
    if(strcmp(expected,s_order.identity)!=0 || s_order.total!=s_url_tracks.total_count ||
       target>=s_url_tracks.total_count || target-s_request_offset>=s_url_tracks.count) {
#ifdef CONFIG_NETEASE_DIRECT
        netease_direct_invalidate_tracks();
#endif
        if(!bsp_lvgl_lock(0))return ESP_ERR_TIMEOUT;
        release_track_cache();bsp_lvgl_unlock();
        if(!s_url_tracks.total_count)return ESP_ERR_NOT_FOUND;
        if(target>=s_url_tracks.total_count)target=0;
        s_request_offset=(target/NETEASE_APP_TRACK_PAGE_SIZE)*NETEASE_APP_TRACK_PAGE_SIZE;
        err=load_tracks();if(err!=ESP_OK)return err;
        if(target-s_request_offset>=s_url_tracks.count)return ESP_ERR_NOT_FOUND;
        music_playback_order_clear(&s_candidate_order);s_candidate_valid=false;
        err=bind_playback_order(&s_candidate_order,s_mode,&s_url_tracks,target);
        if(err!=ESP_OK)return err;
        s_candidate_valid=true;s_order_selection_valid=false;
    }
    s_url_index=target-s_request_offset;s_order_load_prepared=true;
    return load_track_url();
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        if (s_exit_requested) {
            break;
        }
        maintain_wifi();
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
            err=stop_audio_for_track_change();
            if(err==ESP_OK)err=load_tracks();
            if(err==ESP_OK && s_url_tracks.count) { s_url_index=s_page_last?s_url_tracks.count-1:0;err=load_track_url(); }
            else if(err==ESP_OK && bsp_lvgl_lock(0)) {
                s_playback_ended=true;s_logic.playing=false;
                if(!s_detached_work) { s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2; }
                bsp_lvgl_unlock();
            }
        } else if (work == NETEASE_WORK_LOAD_TRACK_URL) {
            if(!s_sta_got_ip)err=start_wifi();
            if(err==ESP_OK)err = load_track_url();
        } else if(work==NETEASE_WORK_ORDER_STEP) {
            if(!s_sta_got_ip)err=start_wifi();
            if(err==ESP_OK)err=step_playback_order();
        } else if(work==NETEASE_WORK_SAVE_MODE) {
            err=save_playback_mode();
        } else if (work == NETEASE_WORK_RESET_WIFI) {
            audio_stream_stop();
            wifi_provisioning_page_request_setup();
            esp_restart();
        } else if (work == NETEASE_WORK_SAVE_DISPLAY) {
            err=display_settings_save();
        }

#ifdef CONFIG_NETEASE_DIRECT
        /* Explicit browsing may reopen API TLS while audio remains active. */
        if (audio_stream_is_active()) netease_direct_client_release_connection();
#endif
        if (err != ESP_OK) {
#ifdef CONFIG_NETEASE_DIRECT
            if (err == NETEASE_DIRECT_ERR_AUTH && !s_exit_requested) {
                /* Keep the error explicit; do not steal a detached view for QR login. */
                s_session_expired=true;
                s_retry_work=NETEASE_WORK_START_QR;
            }
#endif
        }
        if (err != ESP_OK && !s_exit_requested &&
#ifdef CONFIG_NETEASE_DIRECT
            !s_platform_return_requested &&
#endif
            work!=NETEASE_WORK_PREFETCH_TRACKS && work!=NETEASE_WORK_SAVE_MODE) {
            s_retry_work=s_session_expired?NETEASE_WORK_START_QR:work==NETEASE_WORK_NEXT_PLAYING_PAGE && s_url_tracks.count?NETEASE_WORK_LOAD_TRACK_URL:work;
            s_error = err;
            ESP_LOGE(TAG, "Music request failed: %s (0x%x)", esp_err_to_name(err),(unsigned)err);
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
#ifdef CONFIG_NETEASE_DIRECT
            if(s_platform_return_requested && !s_exit_requested) {
                s_qr_refresh_requested=false;s_qr_payload[0]=0;s_work=NETEASE_WORK_START_QR;xTaskNotifyGive(s_worker_task);
            }
#endif
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
    music_playback_order_clear(&s_order);music_playback_order_clear(&s_candidate_order);
#ifdef CONFIG_NETEASE_DIRECT
    music_provider_close();
#endif
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
        s_order_load_prepared=false;
        s_url_index=s_logic.selected;
        if(s_logic.state==NETEASE_APP_PLAYING || s_logic.state==NETEASE_APP_ENDED) {
            s_url_tracks=s_playing_tracks;s_request_playlist=s_playing_playlist_id;s_request_offset=s_playing_offset;
            copy_limited(s_request_heading,sizeof(s_request_heading),s_playing_heading);
        } else s_url_tracks=s_tracks;
    }
    if(work==NETEASE_WORK_NEXT_PLAYING_PAGE) {
        memset(&s_url_tracks,0,sizeof(s_url_tracks));
        s_order_load_prepared=false;s_order_selection_valid=false;
    }
    if(work==NETEASE_WORK_LOAD_TRACKS) {
        for(size_t i=0;s_track_cache && i<3;i++) {
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
#ifdef CONFIG_NETEASE_DIRECT
    if(s_logic.state==NETEASE_APP_PLATFORM)return DEVICE_MUSIC_SCREEN_TRACKS;
#endif
    netease_work_t loading=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
    if(!s_detached_work && (loading==NETEASE_WORK_LOAD_LIBRARY || loading==NETEASE_WORK_LOAD_TRACKS || loading==NETEASE_WORK_NEXT_PLAYING_PAGE || loading==NETEASE_WORK_LOAD_TRACK_URL || loading==NETEASE_WORK_ORDER_STEP ||
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
    case NETEASE_APP_BRIGHTNESS:case NETEASE_APP_SCREEN_TIMEOUT:case NETEASE_APP_NETWORK:case NETEASE_APP_ACCOUNT:
    case NETEASE_APP_LOGOUT_CONFIRM:case NETEASE_APP_WIFI_CONFIRM:case NETEASE_APP_ABOUT:case NETEASE_APP_ENDED:
    case NETEASE_APP_PLAYBACK_MODE:
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
        rows[row].playing = s_logic.has_player &&
            (s_tracks.items[index].qq_mid[0]?strcmp(s_tracks.items[index].qq_mid,s_now_playing.qq_mid)==0:s_tracks.items[index].id==s_now_playing.id);
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
    device_music_ui_set_playback_mode(s_mode==MUSIC_PLAYBACK_SHUFFLE);
    device_music_ui_set_buffering(s_audio_started && playing && audio_stream_state()==AUDIO_STREAM_BUFFERING);
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
           s_rendered_buffering != (s_audio_started && s_logic.playing && audio_stream_state()==AUDIO_STREAM_BUFFERING) ||
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

#ifdef CONFIG_NETEASE_DIRECT
    if(s_logic.state==NETEASE_APP_PLATFORM) {
        const char *items[]={"网易云音乐","QQ 音乐"};
        device_music_ui_draw_menu(screen,"选择音乐平台","登录状态将在选择后验证",items,2,s_logic.selected);
        device_music_ui_set_footer(screen,"选择","进入","—");
    } else
#endif
    if (s_logic.state >= NETEASE_APP_NAVIGATION && screen_id!=DEVICE_MUSIC_SCREEN_LOADING_TRACK) {
        const char *const nav[]={"播放模式","返回播放","当前歌曲列表","我的歌单","音量","设置"};
        const char *const modes[]={s_mode==MUSIC_PLAYBACK_SEQUENTIAL?"顺序播放 · 当前":"顺序播放",
                                  s_mode==MUSIC_PLAYBACK_SHUFFLE?"随机播放 · 当前":"随机播放","返回导航"};
        const char *const settings[]={"显示与日夜","网络","账号","设备信息","退出登录","切换音乐平台"};
        const char *const appearance[]={"背光亮度","熄屏","返回设置"};
        const char *const timeout_names[]={"永不","15 秒","30 秒","1 分钟","2 分钟","5 分钟"};
        char timeout_rows[DISPLAY_TIMEOUT_OPTION_COUNT][32];
        const char *timeouts[DISPLAY_TIMEOUT_OPTION_COUNT+1];
        for(size_t i=0;i<DISPLAY_TIMEOUT_OPTION_COUNT;i++) {
            snprintf(timeout_rows[i],sizeof(timeout_rows[i]),"%s%s",timeout_names[i],i==s_logic.timeout_option?" · 当前":"");
            timeouts[i]=timeout_rows[i];
        }
        timeouts[DISPLAY_TIMEOUT_OPTION_COUNT]="返回显示";
        const char *const network[]={"重新配网","返回设置"};
        const char *const account[]={"退出当前账号","返回设置"};
        const char *const logout[]={"取消，保留登录","确认退出账号"};
        const char *const reset[]={"取消，保留网络","确认重新配网"};
        const char *const ended[]={"从头播放","返回歌曲列表"};
        const char *const *items=nav;size_t n=6;const char *title="翻到哪一页？";const char *detail=NULL;
        switch(s_logic.state) {
        case NETEASE_APP_SETTINGS:items=settings;n=6;title="设置";break;
        case NETEASE_APP_PLAYBACK_MODE:items=modes;n=3;title="播放模式";detail=s_mode_status;break;
        case NETEASE_APP_APPEARANCE:items=appearance;n=3;title="显示与日夜";detail=display_settings_clock_status();break;
        case NETEASE_APP_SCREEN_TIMEOUT:items=timeouts;n=DISPLAY_TIMEOUT_OPTION_COUNT+1;title="熄屏";break;
        case NETEASE_APP_NETWORK:items=network;n=2;title="网络";detail=s_saved.ssid;break;
        case NETEASE_APP_ACCOUNT:items=account;n=2;title="账号";detail=s_nickname[0]?s_nickname:"当前音乐账号";break;
        case NETEASE_APP_LOGOUT_CONFIRM:items=logout;n=2;title="退出当前账号？";detail="仅清除此平台本机登录";break;
        case NETEASE_APP_WIFI_CONFIRM:items=reset;n=2;title="重新连接 Wi-Fi？";detail="清除网络，保留各平台登录";break;
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
        if(s_logic.state==NETEASE_APP_BRIGHTNESS)device_music_ui_draw_brightness(screen,display_settings_brightness());
        else if(s_logic.state==NETEASE_APP_NAVIGATION)device_music_ui_draw_menu(screen,title,detail,items,n,s_logic.selected);
        else {
            device_music_ui_preference_t page=DEVICE_MUSIC_PREF_SETTINGS;
            switch(s_logic.state) {
            case NETEASE_APP_APPEARANCE:page=DEVICE_MUSIC_PREF_APPEARANCE;break;
            case NETEASE_APP_SCREEN_TIMEOUT:page=DEVICE_MUSIC_PREF_SCREEN_TIMEOUT;break;
            case NETEASE_APP_NETWORK:page=DEVICE_MUSIC_PREF_NETWORK;break;
            case NETEASE_APP_ACCOUNT:page=DEVICE_MUSIC_PREF_ACCOUNT;break;
            case NETEASE_APP_LOGOUT_CONFIRM:page=DEVICE_MUSIC_PREF_LOGOUT;break;
            case NETEASE_APP_WIFI_CONFIRM:page=DEVICE_MUSIC_PREF_RESET_WIFI;break;
            case NETEASE_APP_ABOUT:page=DEVICE_MUSIC_PREF_ABOUT;break;
            case NETEASE_APP_ENDED:page=DEVICE_MUSIC_PREF_END;break;
            case NETEASE_APP_PLAYBACK_MODE:page=DEVICE_MUSIC_PREF_PLAYBACK_MODE;break;
            default:break;
            }
            device_music_ui_draw_preferences(screen,page,detail,esp_app_get_description()->version,items,n,s_logic.selected);
        }
    } else if(s_logic.state==NETEASE_APP_SIGNED_IN && screen_id!=DEVICE_MUSIC_SCREEN_LOADING_TRACK) {
        device_music_ui_draw_notice(screen,DEVICE_MUSIC_NOTICE_SIGNED_IN,s_nickname);
    } else switch (screen_id) {
    case DEVICE_MUSIC_SCREEN_LOADING_TRACK: {
        netease_work_t loading=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
        bool track_change=loading==NETEASE_WORK_LOAD_TRACK_URL || loading==NETEASE_WORK_ORDER_STEP || loading==NETEASE_WORK_NEXT_PLAYING_PAGE;
        const char *title=track_change?"正在加载音频流":s_track_heading;
        if(s_library_loading || loading==NETEASE_WORK_LOAD_LIBRARY || loading==NETEASE_WORK_START_QR || loading==NETEASE_WORK_SWITCH_ACCOUNT)
            device_music_ui_draw_notice(screen,(s_library_loading || loading==NETEASE_WORK_LOAD_LIBRARY)?DEVICE_MUSIC_NOTICE_LIBRARY:DEVICE_MUSIC_NOTICE_RESTORE,NULL);
        else device_music_ui_draw_progress(screen,screen_id,title,track_change?"获取歌曲与音频地址\n随后连接音频流":"读取歌曲列表\n获取歌名与歌手",1);
        break;
    }
    case DEVICE_MUSIC_SCREEN_QR_LOGIN:
#ifdef CONFIG_NETEASE_DIRECT
        if(music_provider_platform()==MUSIC_PLATFORM_QQ && s_qq_qr_image.data) {
            device_music_ui_draw_qq_qr(screen,&s_qq_qr_image);
            device_music_ui_set_footer(screen,"—","刷新二维码","返回平台");
            break;
        }
#endif
        s_qr = device_music_ui_draw_qr(screen, screen_id, NULL, NULL);
        device_music_ui_set_footer(screen,"—","刷新二维码","返回平台");
        break;
    case DEVICE_MUSIC_SCREEN_LOGIN_WAITING:
#ifdef CONFIG_NETEASE_DIRECT
        if(music_provider_platform()==MUSIC_PLATFORM_QQ) {
            device_music_ui_draw_message(screen,screen_id,"等待手机确认","请在 QQ 音乐 App 确认登录");break;
        }
#endif
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

    if(screen_id==DEVICE_MUSIC_SCREEN_LOGIN_WAITING)device_music_ui_set_footer(screen,"—","等待确认","返回平台");
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
    s_rendered_buffering=s_audio_started && s_logic.playing && audio_stream_state()==AUDIO_STREAM_BUFFERING;
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

static void request_order_step(music_playback_direction_t direction)
{
    s_order_direction=direction;s_skip_unavailable=false;
    request_work(NETEASE_WORK_ORDER_STEP);
    s_request_playlist=s_playing_playlist_id;s_request_offset=s_playing_offset;
    copy_limited(s_request_heading,sizeof(s_request_heading),s_playing_heading);
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
        if (valid && s_audio_started)
            ESP_LOGI(TAG, "Playback Wi-Fi: rssi=%d channel=%u audio_state=%d",
                     ap.rssi, ap.primary, (int)audio_stream_state());
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
            if(s_playing_tracks.metadata_valid) {
                request_order_step(MUSIC_PLAYBACK_NEXT);
            } else if(s_logic.playing_index+1<s_playing_tracks.count) {
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
    release_track_cache();
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
#ifdef CONFIG_NETEASE_DIRECT
    s_platform_selected=false;s_platform_choice=-1;s_platform_return_requested=false;
#endif
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
    init_playback_order(&s_order);init_playback_order(&s_candidate_order);
    s_mode=MUSIC_PLAYBACK_SEQUENTIAL;s_mode_loaded=false;s_mode_status[0]=0;
    s_candidate_valid=false;s_order_selection_valid=false;s_order_load_prepared=false;s_skip_unavailable=false;
    s_logic.timeout_option=display_timeout_option(display_settings_timeout());
    s_retry_work=NETEASE_WORK_NONE;
    s_active_work=NETEASE_WORK_NONE;
    s_display_dirty=false;s_detached_work=false;s_audio_started=false;s_playback_ended=false;
    s_prefetch_retry_us=0;
    s_library_loading=false;s_session_expired=false;s_qr_expired=false;s_qr_refresh_requested=false;s_browse_last=false;s_request_cursor=0;
    s_nickname[0]='\0';s_background_error=ESP_OK;s_rendered_soc=-2;
    memset(&s_playing_tracks,0,sizeof(s_playing_tracks));memset(&s_now_playing,0,sizeof(s_now_playing));

    render_netease_ui();
    s_timer = lv_timer_create(tick, 250, NULL);

    if (s_worker_task == NULL) {
        if (xTaskCreate(worker_task, "netease_app", 8192, NULL, 4, &s_worker_task) != pdPASS) {
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
void netease_app_refresh_display(void)
{
    s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;render_netease_ui();
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
#ifdef CONFIG_NETEASE_DIRECT
    if(s_logic.state==NETEASE_APP_PLATFORM) {
        if(netease_app_logic_on_button(&s_logic,button,event)==NETEASE_APP_ACTION_SELECT_PLATFORM)
            s_platform_choice=(int)s_logic.selected;
        render_netease_ui();return;
    }
    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_LONG &&
       (s_logic.state==NETEASE_APP_LOGIN_QR || s_logic.state==NETEASE_APP_LOGIN_WAITING || s_logic.state==NETEASE_APP_ERROR)) {
        s_platform_return_requested=true;
        if(!s_worker_busy) { s_work=NETEASE_WORK_START_QR;xTaskNotifyGive(s_worker_task); }
        return;
    }
#endif
    bool work_pending = (s_worker_busy && s_active_work!=NETEASE_WORK_PREFETCH_TRACKS) || s_work != NETEASE_WORK_NONE;
    if(work_pending) {
        netease_work_t active=s_work!=NETEASE_WORK_NONE?s_work:s_active_work;
        if((active==NETEASE_WORK_START_QR || active==NETEASE_WORK_SWITCH_ACCOUNT) && s_logic.state==NETEASE_APP_LOGIN_QR && s_qr_payload[0] && button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK) {
            s_qr_refresh_requested=true;s_qr_payload[0]='\0';render_netease_ui();return;
        }
        if(active!=NETEASE_WORK_LOAD_LIBRARY && active!=NETEASE_WORK_LOAD_TRACKS && active!=NETEASE_WORK_LOAD_TRACK_URL && active!=NETEASE_WORK_NEXT_PLAYING_PAGE && active!=NETEASE_WORK_ORDER_STEP)return;
        if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_LONG)s_detached_work=true;
        if(!s_detached_work)return;
        netease_app_logic_t candidate=s_logic;
        netease_app_action_t pending_action=netease_app_logic_on_button(&candidate,button,event);
        if(pending_action==NETEASE_APP_ACTION_SET_SCREEN_TIMEOUT) {
            s_logic=candidate;
            display_settings_set_timeout(display_timeout_seconds(s_logic.timeout_option));
            request_work(NETEASE_WORK_SAVE_DISPLAY);s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;render_netease_ui();return;
        }
        if(pending_action!=NETEASE_APP_ACTION_NONE || candidate.volume_overlay || candidate.state==NETEASE_APP_TRACKS)return;
        s_logic=candidate;render_netease_ui();return;
    }

    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.state==NETEASE_APP_SIGNED_IN) {
        request_work(NETEASE_WORK_LOAD_LIBRARY);render_netease_ui();return;
    }
#ifdef CONFIG_NETEASE_DIRECT
    if(button==NETEASE_APP_BTN_OK && event==NETEASE_APP_BTN_CLICK && s_logic.state==NETEASE_APP_ERROR && s_error==NETEASE_DIRECT_ERR_UNAVAILABLE) {
        if(s_url_tracks.metadata_valid) {
            s_order_direction=MUSIC_PLAYBACK_NEXT;s_skip_unavailable=true;
            s_detached_work=false;s_work=NETEASE_WORK_ORDER_STEP;
            xTaskNotifyGive(s_worker_task);render_netease_ui();return;
        }
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
       button!=NETEASE_APP_BTN_OK && s_playing_tracks.metadata_valid) {
        request_order_step(button==NETEASE_APP_BTN_DOWN?MUSIC_PLAYBACK_NEXT:MUSIC_PLAYBACK_PREVIOUS);
        render_netease_ui();return;
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
    if(previous_state==NETEASE_APP_NAVIGATION && previous_selected==1 && s_background_error!=ESP_OK && s_logic.state==NETEASE_APP_PLAYING) {
        netease_app_logic_set_error(&s_logic);
    }
    if(previous_state==NETEASE_APP_NAVIGATION && previous_selected==1 && s_playback_ended && s_logic.state==NETEASE_APP_PLAYING) {
        s_logic.state=NETEASE_APP_ENDED;s_logic.selected=0;s_logic.item_count=2;
    }
    if(((previous_state==NETEASE_APP_NAVIGATION && previous_selected==2) || previous_state==NETEASE_APP_ENDED) && s_logic.has_player && button==NETEASE_APP_BTN_OK && s_logic.state==NETEASE_APP_TRACKS) {
        s_tracks=s_playing_tracks;s_track_playlist_id=s_playing_playlist_id;s_track_offset=s_playing_offset;
        copy_limited(s_track_heading,sizeof(s_track_heading),s_playing_heading);
        s_logic.track_cursor=s_logic.playing_index;netease_app_logic_set_tracks(&s_logic,s_tracks.count);
    }
    if (action == NETEASE_APP_ACTION_RESET_CLOUD_SETUP) {
        request_work(NETEASE_WORK_RESET_WIFI);return;
    }
#ifdef CONFIG_NETEASE_DIRECT
    if(action==NETEASE_APP_ACTION_SWITCH_PLATFORM) {
        s_platform_return_requested=true;s_work=NETEASE_WORK_START_QR;xTaskNotifyGive(s_worker_task);return;
    }
#endif
    if(action==NETEASE_APP_ACTION_SET_PLAYBACK_MODE) {
        s_requested_mode=(music_playback_mode_t)s_logic.selected;
        s_mode_status[0]=0;
        request_work(NETEASE_WORK_SAVE_MODE);
    } else if(action==NETEASE_APP_ACTION_SET_SCREEN_TIMEOUT) {
        display_settings_set_timeout(display_timeout_seconds(s_logic.timeout_option));
        s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;
        request_work(NETEASE_WORK_SAVE_DISPLAY);
    } else if (action == NETEASE_APP_ACTION_BRIGHTNESS_UP || action == NETEASE_APP_ACTION_BRIGHTNESS_DOWN) {
        display_settings_set_brightness(display_settings_brightness()+(action==NETEASE_APP_ACTION_BRIGHTNESS_UP?5:-5));
        s_rendered_screen=DEVICE_MUSIC_SCREEN_COUNT;
        request_work(NETEASE_WORK_SAVE_DISPLAY);
    } else if (action == NETEASE_APP_ACTION_RETRY) {
        size_t failed_index=s_url_index;
        netease_work_t retry=s_retry_work;s_background_error=ESP_OK;
        if(retry==NETEASE_WORK_LOAD_TRACK_URL || retry==NETEASE_WORK_ORDER_STEP) {
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
