/* 仅供实机预检的临时配网页面；凭据只在 RAM 中存活。 */
#include "qqmusic_preflight_wifi.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char page[]=
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>QQ 预检临时联网</title><style>body{font:18px sans-serif;max-width:440px;margin:30px auto;padding:16px}"
    "input,select,button{box-sizing:border-box;width:100%;padding:12px;margin:8px 0}</style>"
    "<h2>临时连接 Wi-Fi</h2><p>仅本次开机有效，不修改原 Wi-Fi 设置。请选择可上网的 2.4 GHz 网络。</p>"
    "<button id=scan type=button>扫描附近网络</button><select id=nets><option value=''>手动输入名称</option></select>"
    "<form id=f><label>网络名称<input id=ssid maxlength=32 required autocomplete=off></label>"
    "<label>密码<input id=p type=password maxlength=64 autocomplete=off></label><button id=go>连接</button></form>"
    "<p id=msg role=status></p><script>const $=id=>document.getElementById(id);"
    "$('nets').onchange=()=>{$('ssid').value=$('nets').value};"
    "$('scan').onclick=async()=>{try{$('scan').disabled=true;const r=await fetch('/scan');if(!r.ok)throw Error('扫描失败');"
    "const a=await r.json();$('nets').replaceChildren(new Option('手动输入名称',''));"
    "a.forEach(s=>$('nets').add(new Option(s,s)));$('msg').textContent='请选择网络'}"
    "catch(e){$('msg').textContent=e.message}finally{$('scan').disabled=false}};"
    "$('f').onsubmit=async e=>{e.preventDefault();$('go').disabled=true;try{"
    "const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:$('ssid').value,password:$('p').value})});"
    "$('msg').textContent=await r.text();if(r.ok)$('p').value='';}"
    "catch(e){$('msg').textContent='连接正在切换，请查看设备屏幕；失败后可重连本热点再试。'}"
    "finally{$('go').disabled=false}};</script></html>";

static esp_err_t root(httpd_req_t *req)
{
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,page,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t scan(httpd_req_t *req)
{
    wifi_scan_config_t config={0};
    esp_err_t err=esp_wifi_scan_start(&config,true);
    if(err!=ESP_OK) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Scan failed; retry later");
    uint16_t count=12;wifi_ap_record_t records[12];
    err=esp_wifi_scan_get_ap_records(&count,records);
    if(err!=ESP_OK) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Cannot read scan results");
    cJSON *list=cJSON_CreateArray();
    if(!list) return ESP_ERR_NO_MEM;
    bool ok=true;
    for(unsigned i=0;i<count && ok;i++) {
        char name[33];memcpy(name,records[i].ssid,32);name[32]=0;
        if(!name[0]) continue;
        cJSON *item=cJSON_CreateString(name);
        if(!item) { ok=false;break; }
        if(!cJSON_AddItemToArray(list,item)) { cJSON_Delete(item);ok=false; }
    }
    char *json=ok?cJSON_PrintUnformatted(list):NULL;cJSON_Delete(list);
    if(!json) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Not enough memory");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    err=httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);free(json);return err;
}
static void clear_secret(void *data,size_t size)
{
    volatile unsigned char *p=data;while(size--) *p++=0;
}
esp_err_t qqmusic_preflight_wifi_saved(wifi_config_t *config)
{
    if(!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t driver=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err=usb_serial_jtag_driver_install(&driver);if(err!=ESP_OK) return err;
        usb_serial_jtag_vfs_use_driver();
    }
    ESP_LOGI("qq_preflight","preflight Wi-Fi serial setup ready");
    char body[513]={0};size_t used=0;bool complete=false;
    int64_t deadline=esp_timer_get_time()+5000000;
    while(esp_timer_get_time()<deadline && used<sizeof(body)-1) {
        char ch;int n=usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(100));
        if(n!=1) continue;
        if(ch=='\n') { complete=true;break; }
        body[used++]=ch;
    }
    esp_err_t err=ESP_ERR_NVS_NOT_FOUND;
    if(used) {
        cJSON *json=complete?cJSON_ParseWithLengthOpts(body,used+1,NULL,true):NULL;
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(json,"ssid");
        cJSON *password=cJSON_GetObjectItemCaseSensitive(json,"password");
        bool valid=cJSON_IsString(ssid) && cJSON_IsString(password) &&
            strlen(ssid->valuestring)>0 && strlen(ssid->valuestring)<=32 && strlen(password->valuestring)<=64;
        if(valid) {
            nvs_handle_t store;err=nvs_open("qq_test_wifi",NVS_READWRITE,&store);
            if(err==ESP_OK) {
                err=nvs_set_str(store,"ssid",ssid->valuestring);
                if(err==ESP_OK) err=nvs_set_str(store,"password",password->valuestring);
                if(err==ESP_OK) err=nvs_commit(store);
                nvs_close(store);
            }
        } else err=ESP_ERR_INVALID_ARG;
        if(cJSON_IsString(password)) clear_secret(password->valuestring,strlen(password->valuestring));
        cJSON_Delete(json);
    }
    clear_secret(body,sizeof(body));
    if(used && err!=ESP_OK) return err;
    nvs_handle_t store;err=nvs_open("qq_test_wifi",NVS_READONLY,&store);
    if(err!=ESP_OK) return err;
    char ssid[33]={0},password[65]={0};size_t length=sizeof(ssid);
    err=nvs_get_str(store,"ssid",ssid,&length);length=sizeof(password);
    if(err==ESP_OK) err=nvs_get_str(store,"password",password,&length);
    nvs_close(store);
    if(err==ESP_OK && !ssid[0]) err=ESP_ERR_INVALID_STATE;
    if(err==ESP_OK) {
        memcpy(config->sta.ssid,ssid,strlen(ssid));
        memcpy(config->sta.password,password,strlen(password));config->sta.pmf_cfg.capable=true;
        ESP_LOGI("qq_preflight","using saved preflight Wi-Fi; original configuration unchanged");
    }
    clear_secret(password,sizeof(password));return err;
}
static esp_err_t submit(httpd_req_t *req)
{
    if(req->content_len<=0 || req->content_len>512)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request length");
    char body[513]={0};size_t received=0;
    while(received<(size_t)req->content_len) {
        int n=httpd_req_recv(req,body+received,req->content_len-received);
        if(n<=0) { clear_secret(body,sizeof(body));return ESP_FAIL; }
        received+=(size_t)n;
    }
    const char *end=NULL;
    cJSON *json=cJSON_ParseWithLengthOpts(body,received+1,&end,true);
    cJSON *ssid=cJSON_GetObjectItemCaseSensitive(json,"ssid");
    cJSON *password=cJSON_GetObjectItemCaseSensitive(json,"password");
    bool valid=cJSON_IsString(ssid) && cJSON_IsString(password) &&
        strlen(ssid->valuestring)>0 && strlen(ssid->valuestring)<=32 && strlen(password->valuestring)<=64;
    wifi_config_t config={0};
    if(valid) {
        memcpy(config.sta.ssid,ssid->valuestring,strlen(ssid->valuestring));
        memcpy(config.sta.password,password->valuestring,strlen(password->valuestring));
        config.sta.pmf_cfg.capable=true;
    }
    if(cJSON_IsString(password)) clear_secret(password->valuestring,strlen(password->valuestring));
    cJSON_Delete(json);clear_secret(body,sizeof(body));
    if(!valid) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid SSID or password length");
    BaseType_t queued=xQueueSend((QueueHandle_t)req->user_ctx,&config,0);
    clear_secret(&config,sizeof(config));
    if(queued!=pdTRUE) {
        httpd_resp_set_status(req,"503 Service Unavailable");
        return httpd_resp_sendstr(req,"Connection in progress; retry later");
    }
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"已接收，正在尝试连接。请查看设备屏幕；连接成功后切回正常网络，用 QQ 音乐 App 扫设备二维码。失败可在本页重新提交。");
}
esp_err_t qqmusic_preflight_wifi_setup(volatile bool *connected,
                                     void (*display)(const char *,const char *))
{
    QueueHandle_t queue=xQueueCreate(1,sizeof(wifi_config_t));
    if(!queue) return ESP_ERR_NO_MEM;
    httpd_handle_t server=NULL;esp_netif_t *ap=esp_netif_create_default_wifi_ap();
    if(!ap) { vQueueDelete(queue);return ESP_ERR_NO_MEM; }
    wifi_config_t config={0};char password[16];
    snprintf(password,sizeof(password),"%08lx",(unsigned long)esp_random());
    memcpy(config.ap.ssid,"Folo-QQ-Test",12);config.ap.ssid_len=12;
    memcpy(config.ap.password,password,strlen(password));config.ap.authmode=WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection=2;config.ap.channel=1;
    esp_err_t err=esp_wifi_disconnect();
    if(err==ESP_ERR_WIFI_NOT_CONNECT) err=ESP_OK;
    if(err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,&config);
    clear_secret(&config,sizeof(config));
    if(err==ESP_OK) {
        httpd_config_t http=HTTPD_DEFAULT_CONFIG();http.max_uri_handlers=3;http.stack_size=6144;
        http.recv_wait_timeout=5;http.send_wait_timeout=5;
        err=httpd_start(&server,&http);
    }
    if(err==ESP_OK) {
        httpd_uri_t routes[]={
            {.uri="/",.method=HTTP_GET,.handler=root},
            {.uri="/scan",.method=HTTP_GET,.handler=scan},
            {.uri="/connect",.method=HTTP_POST,.handler=submit,.user_ctx=queue}
        };
        for(unsigned i=0;i<3 && err==ESP_OK;i++) err=httpd_register_uri_handler(server,&routes[i]);
    }
    if(err==ESP_OK) {
        char message[240];snprintf(message,sizeof(message),"Wi-Fi: Folo-QQ-Test\n密码：%s\nhttp://192.168.4.1\n仅本次开机有效",password);
        display("临时 Wi-Fi 配网",message);
        int64_t deadline=esp_timer_get_time()+600000000;
        err=ESP_ERR_TIMEOUT;
        while(esp_timer_get_time()<deadline) {
            if(xQueueReceive(queue,&config,pdMS_TO_TICKS(250))!=pdTRUE) continue;
            /* 给提交响应留出发送时间，再切换 STA 连接。 */
            vTaskDelay(pdMS_TO_TICKS(500));
            (void)esp_wifi_disconnect();*connected=false;
            esp_err_t attempt=esp_wifi_set_config(WIFI_IF_STA,&config);
            clear_secret(&config,sizeof(config));
            if(attempt==ESP_OK) attempt=esp_wifi_connect();
            if(attempt==ESP_OK) {
                for(unsigned i=0;i<200 && !*connected;i++) vTaskDelay(pdMS_TO_TICKS(100));
                if(*connected) { err=ESP_OK;break; }
            }
            snprintf(message,sizeof(message),"连接失败，可重新提交\n手机连接 Folo-QQ-Test\n密码：%s\nhttp://192.168.4.1",password);
            display("临时 Wi-Fi 配网",message);
        }
    }
    if(server) httpd_stop(server);
    /* 不让 HTTP 配网任务或 AP 内存占用影响后续音频测量。 */
    esp_err_t stopped=esp_wifi_set_mode(WIFI_MODE_STA);
    esp_netif_destroy_default_wifi(ap);
    while(xQueueReceive(queue,&config,0)==pdTRUE) clear_secret(&config,sizeof(config));
    vQueueDelete(queue);clear_secret(password,sizeof(password));clear_secret(&config,sizeof(config));
    return err==ESP_OK?stopped:err;
}
