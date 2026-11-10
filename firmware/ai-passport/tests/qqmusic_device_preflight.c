/* 独立实机验证入口，仅通过-DQQMUSIC_DEVICE_PREFLIGHT=ON选择，不是正式产品入口。 */
#include "audio_stream.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "demo_radio.h"
#include "device_music_ui.h"
#include "qqmusic_client.h"
#include "qqmusic_login.h"
#include "qqmusic_qr_png.h"
#include "qqmusic_preflight_wifi.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"
#include "nvs.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG="qq_preflight";
static lv_obj_t *s_screen;
static lv_image_dsc_t s_qr_image;
static volatile bool s_connected,s_restart,s_toggle;
static qqmusic_client_t s_client;
static char s_wifi_ssid[33];

static void resources(const char *phase)
{
    ESP_LOGI(TAG,"phase=%s free=%u largest=%u minimum=%u stack=%u",phase,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
static void status(const char *title,const char *body)
{
    if(!bsp_lvgl_lock(1000)) return;
    device_music_ui_reset_screen(s_screen,DEVICE_MUSIC_SCREEN_SETUP);
    lv_image_cache_drop(&s_qr_image);
    free((void *)s_qr_image.data);memset(&s_qr_image,0,sizeof(s_qr_image));
    device_music_ui_draw_message(s_screen,DEVICE_MUSIC_SCREEN_SETUP,title,body);
    device_music_ui_set_footer(s_screen,"—","暂停 / 继续","长按重新扫码");
    bsp_lvgl_unlock();
}
static bool inflate_png(const uint8_t *input,size_t input_size,uint8_t *output,size_t output_size)
{
    /* ROM 的 mem_to_mem 包装器把大型解压状态放在栈上，超过预检任务栈。
     * 直接使用底层接口，将有界状态放到堆上并在显示前释放。 */
    tinfl_decompressor *state=malloc(sizeof(*state));
    if(!state) return false;
    tinfl_init(state);size_t consumed=input_size,produced=output_size;
    tinfl_status result=tinfl_decompress(state,input,&consumed,output,output,&produced,
        TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);
    return result==TINFL_STATUS_DONE && consumed==input_size && produced==output_size;
}
static uint32_t big32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static esp_err_t show_qr(const qqmusic_login_qr_t *qr)
{
    if(qr->png_length<24) return ESP_ERR_INVALID_RESPONSE;
    unsigned w=big32(qr->png+16),h=big32(qr->png+20);
    if(!w || !h || w>QQMUSIC_QR_SIDE_MAX || h>QQMUSIC_QR_SIDE_MAX) return ESP_ERR_INVALID_SIZE;
    size_t stride=(w+7)/8,bytes=8+stride*h;
    uint8_t *gray=malloc((size_t)w*h),*packed=calloc(1,bytes);
    if(!gray || !packed) { free(gray);free(packed);return ESP_ERR_NO_MEM; }
    if(!qqmusic_qr_png_decode(qr->png,qr->png_length,inflate_png,gray,(size_t)w*h,&w,&h)) {
        free(gray);free(packed);return ESP_ERR_INVALID_RESPONSE;
    }
    packed[3]=255;memset(packed+4,255,4); /* ARGB8888调色板：不反色、不缩放。 */
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++)
        if(gray[(size_t)y*w+x]) packed[8+(size_t)y*stride+x/8]|=(uint8_t)(0x80>>(x%8));
    free(gray);
    if(!bsp_lvgl_lock(1000)) { free(packed);return ESP_ERR_TIMEOUT; }
    device_music_ui_reset_screen(s_screen,DEVICE_MUSIC_SCREEN_SETUP);
    lv_image_cache_drop(&s_qr_image);free((void *)s_qr_image.data);
    s_qr_image=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_I1,
        .w=w,.h=h,.stride=stride},.data=packed,.data_size=bytes};
    device_music_ui_draw_message(s_screen,DEVICE_MUSIC_SCREEN_SETUP,"QQ 音乐验证","使用 QQ 音乐 App 扫码并确认");
    lv_obj_t *background=lv_obj_create(s_screen);
    lv_obj_remove_style_all(background);lv_obj_set_pos(background,(240-(int)w)/2-6,104);
    lv_obj_set_size(background,w+12,h+12);
    lv_obj_set_style_bg_color(background,lv_color_white(),0);lv_obj_set_style_bg_opa(background,LV_OPA_COVER,0);
    lv_obj_t *image=lv_image_create(s_screen);lv_image_set_src(image,&s_qr_image);
    lv_obj_set_pos(image,(240-(int)w)/2,110);
    device_music_ui_set_footer(s_screen,"—","等待确认","长按刷新");
    bsp_lvgl_unlock();return ESP_OK;
}
static void network_event(void *arg,esp_event_base_t base,int32_t id,void *event)
{
    (void)arg;
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        s_connected=true;
        ESP_LOGI(TAG,"Wi-Fi obtained IP address");
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        s_connected=false;
        const wifi_event_sta_disconnected_t *disconnected=event;
        ESP_LOGW(TAG,"Wi-Fi disconnected: reason=%u",(unsigned)disconnected->reason);
    }
}
static esp_err_t connect_wifi(void)
{
    esp_err_t err=demo_radio_nvs_prepare();if(err!=ESP_OK) return err;
    err=demo_radio_network_prepare();if(err!=ESP_OK) return err;
    wifi_config_t config={0};
    err=qqmusic_preflight_wifi_saved(&config);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) return err;
    bool test_wifi=err==ESP_OK;
    if(!test_wifi) {
    nvs_handle_t nvs;err=nvs_open("netease_wifi",NVS_READONLY,&nvs);if(err!=ESP_OK) return err;
    char ssid[33]={0},password[65]={0};size_t length=sizeof(ssid);
    err=nvs_get_str(nvs,"ssid",ssid,&length);length=sizeof(password);
    if(err==ESP_OK) err=nvs_get_str(nvs,"password",password,&length);
    nvs_close(nvs);if(err!=ESP_OK || !ssid[0]) return err==ESP_OK?ESP_ERR_INVALID_STATE:err;
    memcpy(config.sta.ssid,ssid,strlen(ssid));memcpy(config.sta.password,password,strlen(password));
    config.sta.pmf_cfg.capable=true;memset(password,0,sizeof(password));
    }
    memcpy(s_wifi_ssid,config.sta.ssid,32);s_wifi_ssid[32]=0;
    char message[160];snprintf(message,sizeof(message),"已保存的热点：\n%s\n正在连接并校准时间",s_wifi_ssid);
    status("QQ 音乐验证",message);
    if(!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();err=esp_wifi_init(&init);if(err!=ESP_OK) return err;
    esp_event_handler_instance_t wifi_handler,ip_handler;
    err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,network_event,NULL,&wifi_handler);
    if(err==ESP_OK) err=esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,network_event,NULL,&ip_handler);
    if(err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_STA);
    if(err==ESP_OK) err=esp_wifi_set_ps(WIFI_PS_NONE);
    if(err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_STA,&config);
    memset(&config,0,sizeof(config));
    if(err==ESP_OK) err=esp_wifi_start();
    if(err!=ESP_OK) return err;
    /* 验证入口有界重试，失败明确停下；不修改或擦除现有Wi-Fi配置。 */
    for(unsigned attempt=0;attempt<3;attempt++) {
        ESP_LOGI(TAG,"Wi-Fi connection attempt %u/3",attempt+1);
        err=esp_wifi_connect();if(err!=ESP_OK) return err;
        for(unsigned i=0;i<100 && !s_connected;i++) vTaskDelay(pdMS_TO_TICKS(100));
        if(s_connected) break;
    }
    if(!s_connected) {
        err=qqmusic_preflight_wifi_setup(&s_connected,status);
        if(err!=ESP_OK) return err;
    }
    ESP_LOGI(TAG,"Wi-Fi connected; synchronizing clock");
    if(!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_init();
    }
    for(unsigned i=0;i<150;i++) {
        if(time(NULL)>1735689600) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}
static esp_err_t login(void)
{
    qqmusic_login_qr_t *qr=calloc(1,sizeof(*qr));if(!qr) return ESP_ERR_NO_MEM;
    resources("qr_create");esp_err_t err=qqmusic_login_create_qr(qr);
    qqmusic_login_connection_t *connection=NULL;
    if(err==ESP_OK && !s_restart) err=qqmusic_login_connect(qr->id,&connection);
    int64_t deadline=esp_timer_get_time()+(int64_t)qr->expires_seconds*1000000;
    int64_t ready_deadline=esp_timer_get_time()+15000000;
    while(err==ESP_OK && !s_restart && !qqmusic_login_ready(connection)) {
        qqmusic_login_state_t state;char *unexpected=NULL;
        err=qqmusic_login_poll(connection,&state,&unexpected);
        if(unexpected) { volatile char *p=unexpected;size_t n=strlen(unexpected);while(n--) *p++=0;free(unexpected);err=ESP_ERR_INVALID_RESPONSE; }
        if(esp_timer_get_time()>=ready_deadline && !qqmusic_login_ready(connection)) err=ESP_ERR_TIMEOUT;
    }
    if(err==ESP_OK && !s_restart) err=show_qr(qr);
    memset(qr,0,sizeof(*qr));free(qr);
    if(err==ESP_OK) resources("mqtt_connected");
    else ESP_LOGW(TAG,"QR creation/display or MQTT setup failed: %s",esp_err_to_name(err));
    bool confirmed=false;
    while(err==ESP_OK && !s_restart && esp_timer_get_time()<deadline) {
        qqmusic_login_state_t state;char *credential=NULL;
        err=qqmusic_login_poll(connection,&state,&credential);
        if(err==ESP_OK && state==QQMUSIC_LOGIN_CONFIRMED && !s_restart) {
            resources("credential_exchange");
            /* 已获得最终凭据后先释放扫码连接与图片，再进行NVS及业务请求。 */
            qqmusic_login_close(connection);connection=NULL;status("QQ 音乐验证","正在保存本机登录状态");
            err=qqmusic_client_accept(&s_client,credential);confirmed=err==ESP_OK;
        }
        if(credential) { volatile char *p=credential;size_t n=strlen(credential);while(n--) *p++=0;free(credential); }
        if(err!=ESP_OK || confirmed) break;
        if(state==QQMUSIC_LOGIN_CANCELED || state==QQMUSIC_LOGIN_REJECTED) { err=QQMUSIC_ERR_REJECTED;break; }
        if(state==QQMUSIC_LOGIN_EXPIRED) { err=ESP_ERR_TIMEOUT;break; }
    }
    qqmusic_login_close(connection);
    if(err==ESP_OK && !confirmed) err=s_restart?ESP_ERR_INVALID_STATE:ESP_ERR_TIMEOUT;
    return err;
}
static esp_err_t play_sample(void)
{
    qqmusic_playlists_t *lists=calloc(1,sizeof(*lists));qqmusic_tracks_t *tracks=calloc(1,sizeof(*tracks));
    qqmusic_audio_t *audio=calloc(1,sizeof(*audio));
    if(!lists || !tracks || !audio) { free(lists);free(tracks);free(audio);return ESP_ERR_NO_MEM; }
    resources("playlists");esp_err_t err=qqmusic_client_playlists(&s_client,lists);size_t choice=0;
    if(err!=ESP_OK || !lists->count) { free(lists);free(tracks);free(audio);return err==ESP_OK?ESP_ERR_NOT_FOUND:err; }
    for(size_t i=0;i<lists->count;i++) if(lists->items[i].dir_id==201) { choice=i;break; }
    qqmusic_playlist_t playlist=lists->items[choice];free(lists);uint32_t duration=0;
    bool found=false;uint32_t offset=0;
    for(unsigned page=0;page<8 && !s_restart;page++) {
        err=qqmusic_client_tracks(&s_client,&playlist,offset,tracks);if(err!=ESP_OK) break;
        resources("tracks");
        for(size_t i=0;i<tracks->count && !s_restart;i++) {
            err=qqmusic_client_audio(&s_client,&tracks->items[i],audio);
            if(err==QQMUSIC_ERR_UNAVAILABLE) { ESP_LOGI(TAG,"MP3 unavailable for this account; checking next real track");continue; }
            if(err==ESP_OK) { duration=tracks->items[i].duration_ms;found=true; }
            break;
        }
        if(found || (err!=ESP_OK && err!=QQMUSIC_ERR_UNAVAILABLE) || !tracks->has_more) break;
        offset=tracks->next_offset;
    }
    free(tracks);
    if(!found || s_restart) { free(audio);return s_restart?ESP_ERR_INVALID_STATE:err==ESP_OK?QQMUSIC_ERR_UNAVAILABLE:err; }
    resources("before_audio");status("QQ 音乐验证","正在播放整曲 MP3\nOK 暂停或继续，长按重新扫码");
    err=audio_stream_start(audio->url);memset(audio,0,sizeof(*audio));free(audio);
    if(err!=ESP_OK) return err;
    resources("audio_started");uint32_t played=0;int64_t report=0;
    while(!s_restart) {
        if(s_toggle) {
            s_toggle=false;
            if(audio_stream_state()==AUDIO_STREAM_PAUSED) err=audio_stream_resume();else err=audio_stream_pause();
            if(err!=ESP_OK) break;
        }
        uint32_t now=audio_stream_elapsed_ms();if(now>played) played=now;
        audio_stream_state_t state=audio_stream_state();
        if(state==AUDIO_STREAM_ERROR) { err=ESP_FAIL;break; }
        if(state==AUDIO_STREAM_STOPPED) {
            bool full=played+3000>=duration && played<=duration+5000;
            ESP_LOGI(TAG,"sample finished: elapsed_ms=%lu metadata_ms=%lu duration_match=%s",(unsigned long)played,(unsigned long)duration,full?"yes":"no");
            err=full?ESP_OK:ESP_ERR_INVALID_RESPONSE;break;
        }
        if(esp_timer_get_time()-report>=10000000) { resources("playing");report=esp_timer_get_time(); }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    audio_stream_stop();return s_restart?ESP_ERR_INVALID_STATE:err;
}
static void worker(void *arg)
{
    (void)arg;status("QQ 音乐验证","连接已保存的 Wi-Fi 并校准时间");
    esp_err_t err=connect_wifi();
    if(err!=ESP_OK) {
        ESP_LOGE(TAG,"network setup failed: %s",esp_err_to_name(err));
        char message[160];snprintf(message,sizeof(message),"已保存的热点：\n%s\n开启后长按 OK 重试",s_wifi_ssid[0]?s_wifi_ssid:"读取配置失败");
        status("连接失败",message);
        while(!s_restart) vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    qqmusic_client_init(&s_client);bool force=false;
    for(;;) {
        s_restart=false;s_toggle=false;
        resources("restore");
        err=force?ESP_ERR_NVS_NOT_FOUND:qqmusic_client_restore(&s_client);
        if(err==ESP_ERR_NVS_NOT_FOUND || err==QQMUSIC_ERR_AUTH) err=login();
        if(err==ESP_OK && !s_restart) {
            resources("refresh");err=qqmusic_client_refresh(&s_client);
            if(err==ESP_OK) err=qqmusic_client_verify(&s_client);
        }
        if(err==ESP_OK && !s_restart) err=play_sample();
        if(!s_restart) {
            resources("finished");ESP_LOGI(TAG,"preflight result: %s",esp_err_to_name(err));
            status(err==ESP_OK?"本轮播放结束":"验证未通过",err==ESP_OK?"请结合串口记录核对听感\n长按 OK 可重新扫码验证":"错误已写入串口，不含账号凭据\n长按 OK 可重新扫码验证");
        }
        while(!s_restart) vTaskDelay(pdMS_TO_TICKS(100));
        audio_stream_stop();qqmusic_client_close(&s_client);force=true;
    }
}
static void key(bsp_btn_t button,bsp_btn_ev_t event,void *arg)
{
    (void)arg;
    if(button!=BSP_BTN_OK) return;
    if(event==BSP_BTN_LONG) s_restart=true;
    if(event==BSP_BTN_CLICK) s_toggle=true;
}
void app_main(void)
{
    bsp_i2c_init();
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init()) { ESP_LOGE(TAG,"display init failed");return; }
    bsp_display_backlight(100);bsp_battery_init();
    if(!bsp_lvgl_lock(1000)) return;
    s_screen=device_music_ui_create_screen(DEVICE_MUSIC_SCREEN_SETUP);lv_screen_load(s_screen);bsp_lvgl_unlock();
    esp_err_t err=bsp_button_init(key,NULL);
    if(err!=ESP_OK) { ESP_LOGE(TAG,"button init failed: %s",esp_err_to_name(err));return; }
    if(xTaskCreate(worker,"qq_preflight",8192,NULL,4,NULL)!=pdPASS) status("无法启动验证","设备内存不足");
}
