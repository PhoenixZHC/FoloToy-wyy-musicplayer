#include "display_settings.h"
#include "bsp_display.h"
#include "device_music_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <stdlib.h>
#include <time.h>

static const char *TAG="display_settings";
static display_policy_t s_policy;
static int s_brightness=70;
static bool s_ready,s_clock_valid;
static void (*s_refresh)(void);

static void tick(lv_timer_t *timer)
{
    (void)timer;
    time_t now=time(NULL);struct tm local={0};
    bool valid=now>DISPLAY_VALID_CLOCK_EPOCH && localtime_r(&now,&local)!=NULL;
    device_music_ui_set_clock(local.tm_hour,local.tm_min,valid);
    bool night=display_policy_night(local.tm_hour,valid,device_music_ui_is_night());
    bool changed=night!=device_music_ui_is_night() || valid!=s_clock_valid;
    s_clock_valid=valid;
    if(changed) {
        device_music_ui_set_night(night);
        ESP_LOGI(TAG,"Automatic theme: clock=%s theme=%s",valid?"valid":"waiting",night?"night":"day");
        if(s_refresh)s_refresh();
    }
    if(display_policy_tick(&s_policy,esp_timer_get_time())) {
        bsp_display_backlight(0);
        ESP_LOGI(TAG,"Auto screen off: idle=%u seconds",s_policy.timeout_seconds);
    }
}

esp_err_t display_settings_init(void (*refresh)(void))
{
    if(s_ready)return ESP_ERR_INVALID_STATE;
    if(setenv("TZ","CST-8",1)!=0)return ESP_ERR_NO_MEM;
    tzset();
    uint8_t night=0,brightness=70;uint16_t timeout=DISPLAY_DEFAULT_TIMEOUT_SECONDS;
    nvs_handle_t nvs;esp_err_t err=nvs_open("music_display",NVS_READONLY,&nvs);
    if(err==ESP_OK) {
        esp_err_t read=nvs_get_u8(nvs,"night",&night);
        if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Read previous theme: %s",esp_err_to_name(read));
        read=nvs_get_u8(nvs,"brightness",&brightness);
        if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Read brightness: %s",esp_err_to_name(read));
        read=nvs_get_u16(nvs,"screen_timeout",&timeout);
        if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Read screen timeout: %s",esp_err_to_name(read));
        nvs_close(nvs);
    } else if(err!=ESP_ERR_NVS_NOT_FOUND)return err;
    if(brightness<10 || brightness>100) { ESP_LOGW(TAG,"Invalid saved brightness; using 70%%");brightness=70; }
    if(!display_timeout_valid(timeout)) { ESP_LOGW(TAG,"Invalid saved timeout; using 60 seconds");timeout=DISPLAY_DEFAULT_TIMEOUT_SECONDS; }
    s_brightness=brightness;
    display_policy_init(&s_policy,timeout,esp_timer_get_time());
    device_music_ui_set_night(night!=0);bsp_display_backlight(s_brightness);
    s_refresh=refresh;
    if(!lv_timer_create(tick,250,NULL))return ESP_ERR_NO_MEM;
    s_ready=true;
    ESP_LOGI(TAG,"Display ready: timeout=%u seconds; timezone=UTC+8; day=07:00-19:00",timeout);
    return ESP_OK;
}

bool display_settings_on_key(bsp_btn_ev_t event)
{
    if(!s_ready)return false;
    int64_t now=esp_timer_get_time();
    display_policy_activity(&s_policy,now);
    if(event!=BSP_BTN_CLICK && event!=BSP_BTN_LONG && event!=BSP_BTN_DOUBLE)return s_policy.screen_off;
    if(!display_policy_wake(&s_policy,now))return false;
    bsp_display_backlight(s_brightness);
    if(s_refresh)s_refresh();
    ESP_LOGI(TAG,"Display awake; wake key consumed");
    return true;
}
int display_settings_brightness(void) { return s_brightness; }
void display_settings_set_brightness(int percent)
{
    s_brightness=percent<10?10:percent>100?100:percent;
    bsp_display_backlight(s_policy.screen_off?0:s_brightness);
}
uint16_t display_settings_timeout(void) { return s_policy.timeout_seconds; }
void display_settings_set_timeout(uint16_t seconds)
{
    if(!display_timeout_valid(seconds)) { ESP_LOGE(TAG,"Unsupported screen timeout: %u",seconds);return; }
    s_policy.timeout_seconds=seconds;
    display_policy_activity(&s_policy,esp_timer_get_time());
    ESP_LOGI(TAG,"Screen timeout set: %u seconds",seconds);
}
const char *display_settings_clock_status(void)
{
    if(!s_clock_valid)return "等待联网校时 · 暂用上次配色";
    return device_music_ui_is_night()?"北京时间 · 自动夜间":"北京时间 · 自动日间";
}
esp_err_t display_settings_save(void)
{
    if(!bsp_lvgl_lock(1000))return ESP_ERR_TIMEOUT;
    uint8_t brightness=s_brightness,night=device_music_ui_is_night();
    uint16_t timeout=s_policy.timeout_seconds;
    bsp_lvgl_unlock();
    nvs_handle_t nvs;esp_err_t err=nvs_open("music_display",NVS_READWRITE,&nvs);
    if(err!=ESP_OK)return err;
    err=nvs_set_u8(nvs,"night",night);
    if(err==ESP_OK)err=nvs_set_u8(nvs,"brightness",brightness);
    if(err==ESP_OK)err=nvs_set_u16(nvs,"screen_timeout",timeout);
    if(err==ESP_OK)err=nvs_commit(nvs);
    nvs_close(nvs);return err;
}
