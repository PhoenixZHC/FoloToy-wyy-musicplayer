#include "qqmusic_login.h"
#include "qqmusic_mqtt.h"
#include "qqmusic_json.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "mbedtls/base64.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_RESPONSE_MAX 16384
static const char *TAG="qq_preflight_login";
struct qqmusic_login_connection {
    esp_transport_handle_t ssl,ws;
    char qr_id[129];
    uint8_t rx[QQMUSIC_MQTT_PACKET_MAX];
    size_t used;
    bool connected,subscribed;
    unsigned redirects,keepalive;
    int64_t last_ping_us,ping_sent_us;
};
bool qqmusic_login_ready(const qqmusic_login_connection_t *connection)
{
    return connection && connection->subscribed;
}

/* 始终固定腾讯HTTPS入口，拒绝跳转；响应上限不依赖Content-Length。 */
static esp_err_t login_request(const char *method,cJSON *params,bool exchange,cJSON **data)
{
    *data=NULL;
    cJSON *root=cJSON_CreateObject(),*comm=cJSON_CreateObject(),*request=cJSON_CreateObject();
    if(!root || !comm || !request) { cJSON_Delete(root);cJSON_Delete(comm);cJSON_Delete(request);cJSON_Delete(params);return ESP_ERR_NO_MEM; }
    cJSON_AddItemToObject(root,"comm",comm);cJSON_AddItemToObject(root,"req_0",request);
    cJSON_AddStringToObject(comm,"ct",exchange?"11":"23");
    cJSON_AddStringToObject(comm,"cv",exchange?"20090008":"0");
    cJSON_AddStringToObject(comm,"format","json");
    if(exchange) { cJSON_AddStringToObject(comm,"v","20090008");cJSON_AddStringToObject(comm,"tmeAppID","qqmusic"); }
    if(exchange)cJSON_AddStringToObject(comm,"tmeLoginType","6");
    cJSON_AddStringToObject(request,"module","music.login.LoginServer");cJSON_AddStringToObject(request,"method",method);
    cJSON_AddItemToObject(request,"param",params);
    char *body=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!body)return ESP_ERR_NO_MEM;
    char *response=malloc(HTTP_RESPONSE_MAX+1);
    if(!response) { memset(body,0,strlen(body));free(body);return ESP_ERR_NO_MEM; }
    esp_http_client_config_t cfg={.url="https://u.y.qq.com/cgi-bin/musicu.fcg",.method=HTTP_METHOD_POST,
        .timeout_ms=15000,.buffer_size=1024,.buffer_size_tx=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&cfg);
    esp_err_t err=http?ESP_OK:ESP_ERR_NO_MEM;size_t used=0;
    if(err==ESP_OK)err=esp_http_client_set_header(http,"Content-Type","application/json");
    if(err==ESP_OK)err=esp_http_client_set_header(http,"Referer","https://y.qq.com/");
    if(err==ESP_OK)err=esp_http_client_set_header(http,"User-Agent","Mozilla/5.0");
    size_t body_length=strlen(body);
    if(err==ESP_OK)err=esp_http_client_open(http,(int)body_length);
    if(err==ESP_OK && esp_http_client_write(http,body,(int)body_length)!=(int)body_length)err=ESP_FAIL;
    memset(body,0,body_length);free(body);
    if(err==ESP_OK) {
        int64_t length=esp_http_client_fetch_headers(http);
        if(length<0 || length>HTTP_RESPONSE_MAX || esp_http_client_get_status_code(http)!=200) {
            ESP_LOGW(TAG,"login HTTP rejected: status=%d length=%" PRId64,esp_http_client_get_status_code(http),length);
            err=length==-ESP_ERR_HTTP_EAGAIN?ESP_ERR_TIMEOUT:
                length<0?ESP_FAIL:length>HTTP_RESPONSE_MAX?ESP_ERR_INVALID_SIZE:ESP_ERR_INVALID_RESPONSE;
        }
    }
    while(err==ESP_OK && used<HTTP_RESPONSE_MAX) {
        int n=esp_http_client_read(http,response+used,(int)(HTTP_RESPONSE_MAX-used));
        if(n<0) { err=ESP_FAIL;break; }
        if(!n) { if(!esp_http_client_is_complete_data_received(http))err=ESP_ERR_INVALID_RESPONSE;break; }
        used+=(size_t)n;
    }
    if(http)esp_http_client_cleanup(http);
    if(err==ESP_OK && used==HTTP_RESPONSE_MAX)err=ESP_ERR_INVALID_SIZE;
    response[used]=0;
    if(err==ESP_OK) {
        const char *end=NULL;
        root=cJSON_ParseWithLengthOpts(response,used+1,&end,true);
        cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
        request=cJSON_GetObjectItemCaseSensitive(root,"req_0");
        cJSON *request_code=cJSON_GetObjectItemCaseSensitive(request,"code");
        cJSON *value=cJSON_GetObjectItemCaseSensitive(request,"data");
        if(!cJSON_IsNumber(code) || code->valuedouble!=0 || !cJSON_IsNumber(request_code) ||
           request_code->valuedouble!=0 || !cJSON_IsObject(value)) {
            ESP_LOGW(TAG,"login API rejected: exchange=%d root_code=%d request_code=%d data_object=%d",
                exchange,cJSON_IsNumber(code)?code->valueint:-1,
                cJSON_IsNumber(request_code)?request_code->valueint:-1,cJSON_IsObject(value));
            err=ESP_ERR_INVALID_RESPONSE;
        }
        else {
            /* musicid 可能超过 double 精确整数范围，保留响应中的十进制数字。 */
            qqmusic_json_t raw,req_slice,data_slice,id_slice;uint64_t exact_id;
            if(exchange && (!qqmusic_json_parse(response,used,&raw) ||
                !qqmusic_json_member(raw,"req_0",&req_slice) ||
                !qqmusic_json_member(req_slice,"data",&data_slice) ||
                !qqmusic_json_member(data_slice,"musicid",&id_slice) ||
                !qqmusic_json_uint(id_slice,&exact_id) || !exact_id)) err=ESP_ERR_INVALID_RESPONSE;
            if(err==ESP_OK && exchange) {
                char digits[21];snprintf(digits,sizeof(digits),"%" PRIu64,exact_id);
                cJSON *number=cJSON_CreateRaw(digits);
                if(!number) err=ESP_ERR_NO_MEM;
                else if(!cJSON_ReplaceItemInObjectCaseSensitive(value,"musicid",number)) {
                    cJSON_Delete(number);err=ESP_ERR_INVALID_RESPONSE;
                }
            }
            if(err==ESP_OK) *data=cJSON_DetachItemViaPointer(request,value);
        }
        cJSON_Delete(root);
    }
    memset(response,0,used);free(response);return err;
}
static uint32_t big32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
esp_err_t qqmusic_login_create_qr(qqmusic_login_qr_t *qr)
{
    if(!qr)return ESP_ERR_INVALID_ARG;
    memset(qr,0,sizeof(*qr));
    cJSON *params=cJSON_CreateObject();if(!params)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(params,"tmeAppID","qqmusic");cJSON_AddNumberToObject(params,"ct",11);cJSON_AddNumberToObject(params,"cv",20090008);
    cJSON *data=NULL;esp_err_t err=login_request("CreateQRCode",params,false,&data);
    if(err!=ESP_OK)return err;
    cJSON *id=cJSON_GetObjectItemCaseSensitive(data,"qrcodeID"),*image=cJSON_GetObjectItemCaseSensitive(data,"qrcode");
    cJSON *expires=cJSON_GetObjectItemCaseSensitive(data,"expiresIn");
    if(!cJSON_IsString(id) || !id->valuestring[0] || strlen(id->valuestring)>=sizeof(qr->id) ||
       !cJSON_IsString(image) || !cJSON_IsNumber(expires) || expires->valuedouble<1 || expires->valuedouble>1800 ||
       expires->valuedouble!=(double)expires->valueint)err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK) {
        const char *encoded=image->valuestring,*prefix="data:image/png;base64,";
        if(!strncmp(encoded,prefix,strlen(prefix)))encoded+=strlen(prefix);
        if(strlen(encoded)>QQMUSIC_QR_PNG_MAX*4/3+4 ||
           mbedtls_base64_decode(qr->png,sizeof(qr->png),&qr->png_length,(const unsigned char *)encoded,strlen(encoded)))err=ESP_ERR_INVALID_SIZE;
    }
    if(err==ESP_OK && (qr->png_length<33 || memcmp(qr->png,"\x89PNG\r\n\x1a\n",8) ||
       memcmp(qr->png+12,"IHDR",4) || !big32(qr->png+16) || big32(qr->png+16)>240 ||
       !big32(qr->png+20) || big32(qr->png+20)>240))err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK) { strcpy(qr->id,id->valuestring);qr->expires_seconds=(unsigned)expires->valueint; }
    cJSON_Delete(data);
    if(err!=ESP_OK)memset(qr,0,sizeof(*qr));
    return err;
}
static void close_transport(qqmusic_login_connection_t *c)
{
    if(c->ws) { esp_transport_close(c->ws);esp_transport_destroy(c->ws);c->ws=NULL; }
    if(c->ssl) { esp_transport_destroy(c->ssl);c->ssl=NULL; }
    c->used=0;c->connected=false;c->subscribed=false;
}
void qqmusic_login_close(qqmusic_login_connection_t *c)
{
    if(!c)return;
    close_transport(c);memset(c,0,sizeof(*c));free(c);
}
static esp_err_t open_transport(qqmusic_login_connection_t *c,const char *path)
{
    close_transport(c);
    c->ssl=esp_transport_ssl_init();
    if(!c->ssl)return ESP_ERR_NO_MEM;
    esp_transport_ssl_crt_bundle_attach(c->ssl,esp_crt_bundle_attach);
    c->ws=esp_transport_ws_init(c->ssl);
    if(!c->ws)return ESP_ERR_NO_MEM;
    esp_transport_ws_config_t cfg={.ws_path=path,.sub_protocol="mqtt",.user_agent="Mozilla/5.0",
        .headers="Origin: https://y.qq.com\r\nReferer: https://y.qq.com/\r\n",.propagate_control_frames=false};
    esp_err_t err=esp_transport_ws_set_config(c->ws,&cfg);
    if(err!=ESP_OK)return err;
    if(esp_transport_connect(c->ws,"mu.y.qq.com",443,15000)<0)return ESP_ERR_HTTP_CONNECT;
    uint8_t *packet=malloc(2048);if(!packet)return ESP_ERR_NO_MEM;
    char client[48];snprintf(client,sizeof(client),"%" PRId64 "%04u",esp_timer_get_time()/1000,(unsigned)(esp_random()%10000));
    size_t length;
    if(qqmusic_mqtt_connect(client,c->qr_id,packet,2048,&length)!=QQMUSIC_MQTT_OK)err=ESP_ERR_INVALID_ARG;
    else if(esp_transport_write(c->ws,(char *)packet,(int)length,5000)!=(int)length)err=ESP_FAIL;
    free(packet);c->last_ping_us=esp_timer_get_time();c->ping_sent_us=0;c->keepalive=45;
    return err;
}
esp_err_t qqmusic_login_connect(const char *qr_id,qqmusic_login_connection_t **out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    *out=NULL;
    if(!qr_id || !qr_id[0] || strlen(qr_id)>QQMUSIC_QR_ID_MAX)return ESP_ERR_INVALID_ARG;
    qqmusic_login_connection_t *c=calloc(1,sizeof(*c));if(!c)return ESP_ERR_NO_MEM;
    strcpy(c->qr_id,qr_id);
    esp_err_t err=open_transport(c,"/ws/handshake");
    if(err!=ESP_OK) { qqmusic_login_close(c);return err; }
    *out=c;return ESP_OK;
}
static bool safe_reference(const char *reference)
{
    if(!reference[0] || strlen(reference)>200)return false;
    for(size_t i=0;reference[i];i++) {
        char b=reference[i];
        if(!((b>='a' && b<='z') || (b>='A' && b<='Z') || (b>='0' && b<='9') ||
             b=='.' || b==':' || b=='[' || b==']' || b=='-' || b=='_'))return false;
    }
    return true;
}
static esp_err_t exchange_credentials(qqmusic_login_connection_t *c,const uint8_t *payload,size_t length,char **credential)
{
    if(memchr(payload,0,length)) {
        ESP_LOGW(TAG,"cookie payload contains NUL: bytes=%u",(unsigned)length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *message=cJSON_ParseWithLength((const char *)payload,length);
    cJSON *cookies=cJSON_GetObjectItemCaseSensitive(message,"cookies");
    cJSON *uin=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cookies,"qqmusic_uin"),"value");
    cJSON *key=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cookies,"qqmusic_key"),"value");
    esp_err_t err=ESP_OK;
    char *end=NULL;uint64_t id=0;
    if(!cJSON_IsString(uin) || !cJSON_IsString(key) || !key->valuestring[0] || strlen(key->valuestring)>2048) {
        ESP_LOGW(TAG,"cookie schema: object=%d uin_type=%d key_type=%d key_length=%u",
            cJSON_IsObject(cookies),uin?uin->type:0,key?key->type:0,
            (unsigned)(cJSON_IsString(key)?strlen(key->valuestring):0));
        err=ESP_ERR_INVALID_RESPONSE;
    }
    if(err==ESP_OK) {
        bool digits=true;for(const char *p=uin->valuestring;*p;p++) if(*p<'0' || *p>'9') digits=false;
        errno=0;id=strtoull(uin->valuestring,&end,10);
        if(!digits || errno || !end || *end || !id) {
            ESP_LOGW(TAG,"cookie account identifier rejected: length=%u",(unsigned)strlen(uin->valuestring));
            err=ESP_ERR_INVALID_RESPONSE;
        }
    }
    cJSON *params=err==ESP_OK?cJSON_CreateObject():NULL,*data=NULL;
    if(err==ESP_OK && !params)err=ESP_ERR_NO_MEM;
    if(err==ESP_OK) {
        char digits[21];snprintf(digits,sizeof(digits),"%" PRIu64,id);
        cJSON_AddRawToObject(params,"musicid",digits);cJSON_AddStringToObject(params,"qrCodeID",c->qr_id);cJSON_AddStringToObject(params,"token",key->valuestring);
        err=login_request("Login",params,true,&data);
    }
    cJSON_Delete(message);
    if(err!=ESP_OK) ESP_LOGW(TAG,"credential exchange failed: %s",esp_err_to_name(err));
    if(err==ESP_OK) {
        cJSON *musicid=cJSON_GetObjectItemCaseSensitive(data,"musicid"),*musickey=cJSON_GetObjectItemCaseSensitive(data,"musickey");
        if(!cJSON_IsRaw(musicid) || !cJSON_IsString(musickey) || !musickey->valuestring[0])err=ESP_ERR_INVALID_RESPONSE;
        else { *credential=cJSON_PrintUnformatted(data);if(!*credential)err=ESP_ERR_NO_MEM; }
    }
    cJSON_Delete(data);return err;
}
esp_err_t qqmusic_login_poll(qqmusic_login_connection_t *c,qqmusic_login_state_t *state,char **credential)
{
    if(!c || !state || !credential || !c->ws)return ESP_ERR_INVALID_ARG;
    *credential=NULL;*state=QQMUSIC_LOGIN_WAITING;
    int64_t now=esp_timer_get_time();
    if(c->ping_sent_us && now-c->ping_sent_us>10000000) {
        ESP_LOGW(TAG,"MQTT heartbeat response timed out");
        return ESP_ERR_TIMEOUT;
    }
    if(c->keepalive && !c->ping_sent_us && now-c->last_ping_us>(int64_t)c->keepalive*500000) {
        const char ping[]={ (char)0xc0,0 };
        if(esp_transport_write(c->ws,ping,2,1000)!=2)return ESP_FAIL;
        ESP_LOGI(TAG,"MQTT heartbeat sent");
        c->last_ping_us=now;c->ping_sent_us=now;
    }
    if(c->used) {
        qqmusic_mqtt_packet_t packet;
        qqmusic_mqtt_result_t result=qqmusic_mqtt_parse(c->rx,c->used,&packet);
        if(result!=QQMUSIC_MQTT_OK && result!=QQMUSIC_MQTT_MORE) {
            ESP_LOGW(TAG,"MQTT parse rejected: result=%d bytes=%u",result,(unsigned)c->used);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if(result==QQMUSIC_MQTT_OK) {
            ESP_LOGI(TAG,"MQTT packet type=%u reason=%u",packet.type,packet.reason);
            esp_err_t err=ESP_OK;
            if(packet.type==2) {
                if((packet.reason==0x9c || packet.reason==0x9d) && c->redirects<3 && safe_reference(packet.server_reference)) {
                    char path[272];snprintf(path,sizeof(path),"/ws/handshake/%s",packet.server_reference);
                    c->redirects++;return open_transport(c,path);
                }
                if(packet.reason || c->connected)return ESP_ERR_INVALID_RESPONSE;
                c->connected=true;
                if(packet.has_server_keepalive)c->keepalive=packet.server_keepalive;
                ESP_LOGI(TAG,"MQTT keepalive seconds=%u",c->keepalive);
                uint8_t subscribe[512];size_t length;
                if(qqmusic_mqtt_subscribe(c->qr_id,1,subscribe,sizeof(subscribe),&length)!=QQMUSIC_MQTT_OK)return ESP_ERR_INVALID_ARG;
                if(esp_transport_write(c->ws,(char *)subscribe,(int)length,1000)!=(int)length)return ESP_FAIL;
            } else if(packet.type==9) {
                if(!c->connected || c->subscribed || packet.packet_id!=1 || packet.reason>1)return ESP_ERR_INVALID_RESPONSE;
                c->subscribed=true;
            } else if(packet.type==3) {
                char topic[160];int n=snprintf(topic,sizeof(topic),"management.qrcode_login/%s",c->qr_id);
                if(!c->subscribed || packet.topic_length!=(size_t)n || memcmp(packet.topic,topic,(size_t)n))return ESP_ERR_INVALID_RESPONSE;
                if(packet.qos) {
                    char ack[]={0x40,2,(char)(packet.packet_id>>8),(char)packet.packet_id};
                    if(esp_transport_write(c->ws,ack,sizeof(ack),1000)!=(int)sizeof(ack))return ESP_FAIL;
                }
                if(!strcmp(packet.event,"scanned"))*state=QQMUSIC_LOGIN_SCANNED;
                else if(!strcmp(packet.event,"canceled"))*state=QQMUSIC_LOGIN_CANCELED;
                else if(!strcmp(packet.event,"timeout"))*state=QQMUSIC_LOGIN_EXPIRED;
                else if(!strcmp(packet.event,"loginFailed"))*state=QQMUSIC_LOGIN_REJECTED;
                else if(!strcmp(packet.event,"cookies")) {
                    err=exchange_credentials(c,packet.payload,packet.payload_length,credential);
                    if(err==ESP_OK)*state=QQMUSIC_LOGIN_CONFIRMED;
                } else return ESP_ERR_INVALID_RESPONSE;
            } else if(packet.type==13)c->ping_sent_us=0;
            else return ESP_ERR_INVALID_RESPONSE;
            memmove(c->rx,c->rx+packet.consumed,c->used-packet.consumed);c->used-=packet.consumed;
            return err;
        }
    }
    if(c->used==sizeof(c->rx))return ESP_ERR_INVALID_SIZE;
    int ready=esp_transport_poll_read(c->ws,250);
    if(ready<0)return ESP_FAIL;
    if(!ready)return ESP_OK;
    int n=esp_transport_read(c->ws,(char *)c->rx+c->used,(int)(sizeof(c->rx)-c->used),1000);
    if(n<0)return ESP_FAIL;
    /* SDK在内部应答WebSocket PING/PONG；这些返回值不是MQTT载荷。 */
    ws_transport_opcodes_t opcode=esp_transport_ws_get_read_opcode(c->ws);
    if(opcode==WS_TRANSPORT_OPCODES_CLOSE)return ESP_ERR_INVALID_STATE;
    if(opcode==WS_TRANSPORT_OPCODES_PING || opcode==WS_TRANSPORT_OPCODES_PONG || n==0)return ESP_OK;
    if(opcode!=WS_TRANSPORT_OPCODES_BINARY && opcode!=WS_TRANSPORT_OPCODES_CONT) {
        ESP_LOGW(TAG,"Unexpected WebSocket opcode=%u bytes=%d",(unsigned)opcode,n);
        return ESP_ERR_INVALID_RESPONSE;
    }
    c->used+=(size_t)n;return ESP_OK;
}
