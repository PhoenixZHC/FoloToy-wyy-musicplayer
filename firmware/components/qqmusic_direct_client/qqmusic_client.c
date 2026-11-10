#include "qqmusic_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

static void secret_free(char *text)
{
    if(text) { volatile unsigned char *p=(volatile unsigned char *)text;size_t n=strlen(text);while(n--) *p++=0;free(text); }
}
void qqmusic_client_init(qqmusic_client_t *client) { if(client) memset(client,0,sizeof(*client)); }
void qqmusic_client_close(qqmusic_client_t *client)
{
    if(client) { secret_free(client->credential);memset(client,0,sizeof(*client)); }
}
static bool string_field(qqmusic_json_t object,const char *key,char *out,size_t capacity)
{
    qqmusic_json_t value;return qqmusic_json_member(object,key,&value) && qqmusic_json_string(value,out,capacity);
}
static bool add_uint(cJSON *object,const char *name,uint64_t value)
{
    char digits[32];snprintf(digits,sizeof(digits),"%llu",(unsigned long long)value);
    return cJSON_AddRawToObject(object,name,digits)!=NULL;
}
static esp_err_t request(qqmusic_client_t *client,const char *module,const char *method,cJSON *params,char **response,qqmusic_json_t *data)
{
    *response=NULL;*data=(qqmusic_json_t){0};
    if(!params) return ESP_ERR_NO_MEM;
    if(!client || !client->credential || client->blocked) { cJSON_Delete(params);return ESP_ERR_INVALID_STATE; }
    cJSON *root=cJSON_CreateObject(),*comm=NULL,*call=NULL;
    bool ok=root && (comm=cJSON_AddObjectToObject(root,"comm")) && (call=cJSON_AddObjectToObject(root,"req_0"));
    if(!ok) { cJSON_Delete(params);cJSON_Delete(root);return ESP_ERR_NO_MEM; }
    ok=cJSON_AddItemToObject(call,"param",params);
    if(!ok) { cJSON_Delete(params);cJSON_Delete(root);return ESP_ERR_NO_MEM; }
    qqmusic_json_t credential,login_type;uint64_t type;
    char key[2049];
    if(!qqmusic_json_parse(client->credential,strlen(client->credential),&credential) ||
       !string_field(credential,"musickey",key,sizeof(key)) ||
       !qqmusic_json_member(credential,"loginType",&login_type) || !qqmusic_json_uint(login_type,&type)) {
        memset(key,0,sizeof(key));cJSON_Delete(root);return ESP_ERR_INVALID_STATE;
    }
    ok=cJSON_AddStringToObject(comm,"ct","11") && cJSON_AddStringToObject(comm,"cv","20090008") &&
       cJSON_AddStringToObject(comm,"v","20090008") && cJSON_AddStringToObject(comm,"format","json") &&
       cJSON_AddStringToObject(comm,"tmeAppID","qqmusic") && cJSON_AddStringToObject(comm,"qq",client->account) &&
       cJSON_AddStringToObject(comm,"authst",key) && add_uint(comm,"tmeLoginType",type) &&
       cJSON_AddStringToObject(call,"module",module) && cJSON_AddStringToObject(call,"method",method);
    memset(key,0,sizeof(key));
    char *body=ok?cJSON_PrintUnformatted(root):NULL;cJSON_Delete(root);
    if(!body) return ESP_ERR_NO_MEM;
    const size_t cap=!strcmp(method,"CgiGetDiss")?32768:!strcmp(method,"GetPlaylistByUin")?16384:8192;
    char *raw=malloc(cap+1);if(!raw) { secret_free(body);return ESP_ERR_NO_MEM; }
    raw[0]=0;
    esp_http_client_config_t config={.url="https://u.y.qq.com/cgi-bin/musicu.fcg",.method=HTTP_METHOD_POST,
        .timeout_ms=15000,.buffer_size=1024,.buffer_size_tx=1024,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    esp_err_t err=http?ESP_OK:ESP_ERR_NO_MEM;size_t used=0;
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Content-Type","application/json");
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Referer","https://y.qq.com/");
    if(err==ESP_OK) err=esp_http_client_set_header(http,"User-Agent","Mozilla/5.0");
    size_t length=strlen(body),written=0;
    if(err==ESP_OK) err=esp_http_client_open(http,(int)length);
    while(err==ESP_OK && written<length) {
        int n=esp_http_client_write(http,body+written,(int)(length-written));
        if(n<=0) err=ESP_FAIL;else written+=(size_t)n;
    }
    secret_free(body);
    if(err==ESP_OK) {
        int64_t size=esp_http_client_fetch_headers(http);
        if(size<0 || esp_http_client_get_status_code(http)!=200) err=ESP_ERR_INVALID_RESPONSE;
        else if(size>(int64_t)cap) err=ESP_ERR_INVALID_SIZE;
    }
    while(err==ESP_OK && used<cap) {
        int n=esp_http_client_read(http,raw+used,(int)(cap-used));
        if(n<0) err=ESP_FAIL;
        else if(!n) { if(!esp_http_client_is_complete_data_received(http)) err=ESP_ERR_INVALID_RESPONSE;break; }
        else used+=(size_t)n;
    }
    if(err==ESP_OK && used==cap && !esp_http_client_is_complete_data_received(http)) err=ESP_ERR_INVALID_SIZE;
    if(http) esp_http_client_cleanup(http); /* 不保留闲置API TLS与媒体下载争用堆。 */
    raw[used]=0;
    if(err==ESP_OK) err=qqmusic_parse_response(raw,used,data);
    if(err!=ESP_OK) {
        volatile unsigned char *p=(volatile unsigned char *)raw;
        for(size_t i=0;i<used;i++) p[i]=0;
        free(raw);*data=(qqmusic_json_t){0};
    }
    else *response=raw;
    return err;
}
static esp_err_t persist(const char *json)
{
    nvs_handle_t nvs;esp_err_t err=nvs_open("qqmusic",NVS_READWRITE,&nvs);
    if(err!=ESP_OK) return err;
    err=nvs_set_str(nvs,"session",json);
    if(err==ESP_OK) err=nvs_commit(nvs);
    nvs_close(nvs);return err;
}
esp_err_t qqmusic_client_accept(qqmusic_client_t *client,const char *credential_json)
{
    if(!client || !credential_json) return ESP_ERR_INVALID_ARG;
    size_t length=strnlen(credential_json,QQMUSIC_SESSION_MAX+1);
    char account[32],nickname[64];
    esp_err_t err=qqmusic_parse_credential(credential_json,length,account,nickname);
    if(err!=ESP_OK) return err;
    char *copy=malloc(length+1);if(!copy) return ESP_ERR_NO_MEM;
    memcpy(copy,credential_json,length+1);
    err=persist(copy);
    if(err!=ESP_OK) { secret_free(copy);return err; }
    qqmusic_client_close(client);client->credential=copy;
    strcpy(client->account,account);strcpy(client->nickname,nickname);return ESP_OK;
}
esp_err_t qqmusic_client_verify(qqmusic_client_t *client)
{
    char *raw=NULL;qqmusic_json_t data;
    esp_err_t err=request(client,"music.UserInfo.userInfoServer","GetLoginUserInfo",cJSON_CreateObject(),&raw,&data);
    secret_free(raw);return err;
}
esp_err_t qqmusic_client_refresh(qqmusic_client_t *client)
{
    if(!client || !client->credential || client->blocked) return ESP_ERR_INVALID_STATE;
    qqmusic_json_t credential;
    if(!qqmusic_json_parse(client->credential,strlen(client->credential),&credential)) return ESP_ERR_INVALID_RESPONSE;
    cJSON *params=cJSON_CreateObject();if(!params) return ESP_ERR_NO_MEM;
    const char *fields[]={"openid","access_token","refresh_token","str_musicid","musicid","musickey","unionid","refresh_key"};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
        qqmusic_json_t value;
        if(!qqmusic_json_member(credential,fields[i],&value)) continue;
        if(value.length>QQMUSIC_SESSION_MAX) { cJSON_Delete(params);return ESP_ERR_INVALID_SIZE; }
        char *copy=malloc(value.length+1);if(!copy) { cJSON_Delete(params);return ESP_ERR_NO_MEM; }
        memcpy(copy,value.text,value.length);copy[value.length]=0;
        bool ok=cJSON_AddRawToObject(params,fields[i],copy)!=NULL;secret_free(copy);
        if(!ok) { cJSON_Delete(params);return ESP_ERR_NO_MEM; }
    }
    qqmusic_json_t expiry;uint64_t expires=0;
    if(qqmusic_json_member(credential,"expired_at",&expiry) && !qqmusic_json_uint(expiry,&expires)) { cJSON_Delete(params);return ESP_ERR_INVALID_RESPONSE; }
    if(!add_uint(params,"expired_in",expires) || !cJSON_AddNumberToObject(params,"loginMode",2)) { cJSON_Delete(params);return ESP_ERR_NO_MEM; }
    char *raw=NULL;qqmusic_json_t data;esp_err_t err=request(client,"music.login.LoginServer","Login",params,&raw,&data);
    if(err==ESP_OK) {
        char *updated=malloc(data.length+1);
        if(!updated) err=ESP_ERR_NO_MEM;
        else {
            memcpy(updated,data.text,data.length);updated[data.length]=0;
            char account[32],nick[64];err=qqmusic_parse_credential(updated,data.length,account,nick);
            if(err==ESP_OK && strcmp(account,client->account)) err=ESP_ERR_INVALID_RESPONSE;
            if(err==ESP_OK) err=qqmusic_client_accept(client,updated);
            secret_free(updated);
        }
    }
    secret_free(raw);return err;
}
esp_err_t qqmusic_client_restore(qqmusic_client_t *client)
{
    if(!client) return ESP_ERR_INVALID_ARG;
    client->blocked=true;
    nvs_handle_t nvs;esp_err_t err=nvs_open("qqmusic",NVS_READONLY,&nvs);
    if(err!=ESP_OK) return err;
    size_t length=0;err=nvs_get_str(nvs,"session",NULL,&length);
    if(err!=ESP_OK || length<2 || length>QQMUSIC_SESSION_MAX+1) { nvs_close(nvs);return err==ESP_OK?ESP_ERR_INVALID_SIZE:err; }
    const size_t allocated=length;
    char *json=calloc(1,allocated);if(!json) { nvs_close(nvs);return ESP_ERR_NO_MEM; }
    err=nvs_get_str(nvs,"session",json,&length);nvs_close(nvs);json[allocated-1]=0;
    char account[32],nick[64];
    if(err==ESP_OK) err=qqmusic_parse_credential(json,length-1,account,nick);
    if(err!=ESP_OK) { secret_free(json);return err; }
    qqmusic_client_close(client);client->credential=json;
    strcpy(client->account,account);strcpy(client->nickname,nick);
    err=qqmusic_client_verify(client);
    if(err==QQMUSIC_ERR_AUTH) err=qqmusic_client_refresh(client);
    if(err!=ESP_OK) client->blocked=true; /* 失败保留NVS，禁止未经验证的会话继续请求。 */
    return err;
}
esp_err_t qqmusic_client_logout(qqmusic_client_t *client)
{
    if(!client) return ESP_ERR_INVALID_ARG;
    qqmusic_client_close(client);client->blocked=true;
    nvs_handle_t nvs;esp_err_t err=nvs_open("qqmusic",NVS_READWRITE,&nvs);
    if(err!=ESP_OK) return err;
    err=nvs_erase_key(nvs,"session");if(err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
    if(err==ESP_OK) err=nvs_commit(nvs);
    nvs_close(nvs);return err;
}
static cJSON *account_params(qqmusic_client_t *client,bool encrypted)
{
    if(!client || !client->credential || client->blocked) return NULL;
    cJSON *params=cJSON_CreateObject();if(!params) return NULL;
    char value[256];bool ok=true;
    if(encrypted) {
        qqmusic_json_t root;
        ok=qqmusic_json_parse(client->credential,strlen(client->credential),&root) && string_field(root,"encryptUin",value,sizeof(value)) && value[0];
    } else snprintf(value,sizeof(value),"%s",client->account);
    if(!ok || !cJSON_AddStringToObject(params,"uin",value)) { cJSON_Delete(params);return NULL; }
    return params;
}
esp_err_t qqmusic_client_playlists(qqmusic_client_t *client,qqmusic_playlists_t *out)
{
    if(!out || !client || !client->credential || client->blocked) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));char *raw=NULL;qqmusic_json_t data;
    esp_err_t err=request(client,"music.musicasset.PlaylistBaseRead","GetPlaylistByUin",account_params(client,false),&raw,&data);
    if(err==ESP_OK) err=qqmusic_parse_playlists(data,false,out);
    secret_free(raw);raw=NULL;
    if(err!=ESP_OK || out->count==QQMUSIC_MAX_PLAYLISTS) return err;
    cJSON *params=account_params(client,true);
    if(!params) { memset(out,0,sizeof(*out));return ESP_ERR_INVALID_STATE; }
    if(!cJSON_AddNumberToObject(params,"offset",0) || !cJSON_AddNumberToObject(params,"size",QQMUSIC_MAX_PLAYLISTS-out->count)) { cJSON_Delete(params);memset(out,0,sizeof(*out));return ESP_ERR_NO_MEM; }
    err=request(client,"music.musicasset.PlaylistFavRead","CgiGetPlaylistFavInfo",params,&raw,&data);
    qqmusic_playlists_t *favorites=NULL;
    if(err==ESP_OK) { favorites=malloc(sizeof(*favorites));if(!favorites) err=ESP_ERR_NO_MEM; }
    if(err==ESP_OK) err=qqmusic_parse_playlists(data,true,favorites);
    if(err==ESP_OK) {
        out->has_more=out->has_more || favorites->has_more;
        for(size_t i=0;i<favorites->count;i++) {
            bool duplicate=false;
            for(size_t j=0;j<out->count;j++) if(out->items[j].id==favorites->items[i].id) duplicate=true;
            if(duplicate) continue;
            if(out->count==QQMUSIC_MAX_PLAYLISTS) { out->has_more=true;break; }
            out->items[out->count++]=favorites->items[i];
        }
    }
    free(favorites);secret_free(raw);
    if(err!=ESP_OK) memset(out,0,sizeof(*out));
    return err;
}
esp_err_t qqmusic_client_tracks(qqmusic_client_t *client,const qqmusic_playlist_t *playlist,uint32_t offset,qqmusic_tracks_t *out)
{
    if(!playlist || !out || !client || !client->credential || client->blocked) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));cJSON *params=cJSON_CreateObject();if(!params) return ESP_ERR_NO_MEM;
    bool ok=add_uint(params,"disstid",playlist->dir_id==201?0:playlist->id) && add_uint(params,"dirid",playlist->dir_id) &&
        add_uint(params,"song_begin",offset) && add_uint(params,"song_num",QQMUSIC_MAX_TRACKS) &&
        cJSON_AddBoolToObject(params,"tag",false) && cJSON_AddBoolToObject(params,"userinfo",false) && cJSON_AddBoolToObject(params,"orderlist",true);
    if(ok && playlist->dir_id==201) {
        qqmusic_json_t credential;char encrypted[256];
        ok=qqmusic_json_parse(client->credential,strlen(client->credential),&credential) &&
           string_field(credential,"encryptUin",encrypted,sizeof(encrypted)) && encrypted[0] && cJSON_AddStringToObject(params,"enc_host_uin",encrypted);
    }
    if(!ok) { cJSON_Delete(params);return ESP_ERR_NO_MEM; }
    char *raw=NULL;qqmusic_json_t data;esp_err_t err=request(client,"music.srfDissInfo.DissInfo","CgiGetDiss",params,&raw,&data);
    if(err==ESP_OK) err=qqmusic_parse_tracks(data,offset,out);
    secret_free(raw);return err;
}
esp_err_t qqmusic_client_audio(qqmusic_client_t *client,const qqmusic_track_t *track,qqmusic_audio_t *out)
{
    if(!track || !out || !client || !client->credential || client->blocked) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));if(!track->mp3_bytes || !track->media_mid[0] || !track->mid[0]) {
#ifdef ESP_PLATFORM
        ESP_LOGW("qqmusic","Full MP3 metadata missing: bytes=%llu mid=%d media_mid=%d",
                 (unsigned long long)track->mp3_bytes,track->mid[0]!=0,track->media_mid[0]!=0);
#endif
        return QQMUSIC_ERR_UNAVAILABLE;
    }
    char guid[16];snprintf(guid,sizeof(guid),"%lu",(unsigned long)esp_random());
    cJSON *params=cJSON_CreateObject();if(!params) return ESP_ERR_NO_MEM;
    bool ok=cJSON_AddStringToObject(params,"guid",guid) && cJSON_AddStringToObject(params,"uid","0") &&
        cJSON_AddNumberToObject(params,"use_new_domain",1) && cJSON_AddNumberToObject(params,"use_ipv6",0);
    if(!ok) { cJSON_Delete(params);return ESP_ERR_NO_MEM; }
    char *dispatch_raw=NULL;qqmusic_json_t dispatch;
    esp_err_t err=request(client,"music.audioCdnDispatch.cdnDispatch","GetCdnDispatch",params,&dispatch_raw,&dispatch);
    if(err!=ESP_OK) return err;
    params=cJSON_CreateObject();cJSON *filenames=NULL,*mids=NULL,*types=NULL;
    char filename[80];snprintf(filename,sizeof(filename),"M500%s.mp3",track->media_mid);
    ok=params && cJSON_AddStringToObject(params,"uin",client->account) && cJSON_AddStringToObject(params,"guid",guid) &&
       (filenames=cJSON_AddArrayToObject(params,"filename")) && (mids=cJSON_AddArrayToObject(params,"songmid")) &&
       (types=cJSON_AddArrayToObject(params,"songtype")) && cJSON_AddNumberToObject(params,"ctx",0);
    if(ok) {
        cJSON *name=cJSON_CreateString(filename),*mid=cJSON_CreateString(track->mid),*type=cJSON_CreateNumber(track->song_type);
        if(!name || !mid || !type) { cJSON_Delete(name);cJSON_Delete(mid);cJSON_Delete(type);ok=false; }
        else { cJSON_AddItemToArray(filenames,name);cJSON_AddItemToArray(mids,mid);cJSON_AddItemToArray(types,type); }
    }
    if(!ok) { cJSON_Delete(params);secret_free(dispatch_raw);return ESP_ERR_NO_MEM; }
    char *raw=NULL;qqmusic_json_t data;err=request(client,"music.vkey.GetVkey","UrlGetVkey",params,&raw,&data);
    if(err==ESP_OK) err=qqmusic_parse_audio(data,dispatch,track,out);
    secret_free(raw);secret_free(dispatch_raw);return err;
}
