#include "qqmusic_client.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include <inttypes.h>
#endif

static bool number(qqmusic_json_t object,const char *key,uint64_t *out)
{
    qqmusic_json_t value;return qqmusic_json_member(object,key,&value) && qqmusic_json_uint(value,out);
}
static bool text(qqmusic_json_t object,const char *key,char *out,size_t capacity)
{
    qqmusic_json_t value;return qqmusic_json_member(object,key,&value) && qqmusic_json_string(value,out,capacity);
}
static bool display_text(qqmusic_json_t object,const char *key,char *out,size_t capacity)
{
    qqmusic_json_t value;return qqmusic_json_member(object,key,&value) && qqmusic_json_display_string(value,out,capacity);
}
static bool uint32(qqmusic_json_t object,const char *key,uint32_t *out)
{
    uint64_t n;if(!number(object,key,&n) || n>UINT32_MAX) return false;*out=(uint32_t)n;return true;
}
static esp_err_t business(uint64_t code)
{
    if(!code) return ESP_OK;
#ifdef ESP_PLATFORM
    ESP_LOGW("qqmusic", "Service rejected request: code=%" PRIu64,code);
#endif
    if(code==1000 || code==104400 || code==104401) return QQMUSIC_ERR_AUTH;
    if(code==104003 || code==104004 || code==104013) return QQMUSIC_ERR_UNAVAILABLE;
    return QQMUSIC_ERR_REJECTED;
}
static bool valid_mid(const char mid[QQMUSIC_MID_MAX])
{
    if(!mid[0])return false;
    for(size_t i=0;i<QQMUSIC_MID_MAX;i++) {
        unsigned char c=(unsigned char)mid[i];
        if(!c)return true;
        if(!((c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z')))return false;
    }
    return false;
}
esp_err_t qqmusic_cover_url(const qqmusic_track_t *track,char *out,size_t capacity)
{
    if(out && capacity)out[0]=0;
    if(!track || !out || !capacity)return ESP_ERR_INVALID_ARG;
    if(!track->album_mid[0])return ESP_ERR_NOT_FOUND;
    if(!valid_mid(track->album_mid))return ESP_ERR_INVALID_RESPONSE;
    /* 官方CDN实测支持90×90，60×60返回404；图片模块负责有界缩小。 */
    int n=snprintf(out,capacity,"https://y.gtimg.cn/music/photo_new/T002R90x90M000%s.jpg",track->album_mid);
    if(n<0 || (size_t)n>=capacity) { out[0]=0;return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
esp_err_t qqmusic_lyrics_url(const qqmusic_track_t *track,char *out,size_t capacity)
{
    if(out && capacity)out[0]=0;
    if(!track || !out || !capacity)return ESP_ERR_INVALID_ARG;
    if(!valid_mid(track->mid))return ESP_ERR_INVALID_RESPONSE;
    int n=snprintf(out,capacity,"https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=%s&format=json&nobase64=1",track->mid);
    if(n<0 || (size_t)n>=capacity) { out[0]=0;return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
esp_err_t qqmusic_parse_lyrics(const char *json,size_t length,char *out,size_t capacity)
{
    if(out && capacity)out[0]=0;
    if(!json || !out || !capacity)return ESP_ERR_INVALID_ARG;
    if(length>QQMUSIC_LYRICS_RESPONSE_MAX)return ESP_ERR_INVALID_SIZE;
    qqmusic_json_t root,lyric;uint64_t code,retcode,subcode;
    if(!qqmusic_json_parse(json,length,&root) || !number(root,"retcode",&retcode) ||
       !number(root,"code",&code) || !number(root,"subcode",&subcode))return ESP_ERR_INVALID_RESPONSE;
    esp_err_t err=business(retcode?retcode:code?code:subcode);
    if(err!=ESP_OK)return err;
    if(!qqmusic_json_member(root,"lyric",&lyric) || lyric.length<2 || lyric.text[0]!='"')return ESP_ERR_INVALID_RESPONSE;
    size_t limit=capacity>QQMUSIC_LYRICS_MAX_BYTES+1?QQMUSIC_LYRICS_MAX_BYTES+1:capacity;
    if(!qqmusic_json_string(lyric,out,limit)) { out[0]=0;return ESP_ERR_INVALID_SIZE; }
    if(!out[0])return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}
esp_err_t qqmusic_parse_response(const char *json,size_t length,qqmusic_json_t *data)
{
    if(!data) return ESP_ERR_INVALID_ARG;
    *data=(qqmusic_json_t){0};qqmusic_json_t root,request,inner;uint64_t code;
    if(!qqmusic_json_parse(json,length,&root) || !number(root,"code",&code)) return ESP_ERR_INVALID_RESPONSE;
    if(code) return QQMUSIC_ERR_REJECTED;
    if(!qqmusic_json_member(root,"req_0",&request) || !number(request,"code",&code)) return ESP_ERR_INVALID_RESPONSE;
    esp_err_t err=business(code);if(err!=ESP_OK) return err;
    if(!qqmusic_json_member(request,"data",&inner) || !inner.length || inner.text[0]!='{') return ESP_ERR_INVALID_RESPONSE;
    qqmusic_json_t status;
    if(qqmusic_json_member(inner,"code",&status)) {
        if(!qqmusic_json_uint(status,&code)) return ESP_ERR_INVALID_RESPONSE;
        err=business(code);if(err!=ESP_OK) return err;
    }
    *data=inner;return ESP_OK;
}
esp_err_t qqmusic_parse_credential(const char *json,size_t length,char account[32],char nickname[64])
{
    if(!account || !nickname) return ESP_ERR_INVALID_ARG;
    account[0]=nickname[0]=0;
    qqmusic_json_t root;uint64_t musicid,login_type;char key[2049];
    if(length>QQMUSIC_SESSION_MAX || !qqmusic_json_parse(json,length,&root) ||
       !number(root,"musicid",&musicid) || !musicid || !number(root,"loginType",&login_type) ||
       !login_type || login_type>UINT32_MAX || !text(root,"musickey",key,sizeof(key)) || !key[0]) {
        memset(key,0,sizeof(key));return ESP_ERR_INVALID_RESPONSE;
    }
    memset(key,0,sizeof(key));
    char canonical[32];int n=snprintf(canonical,sizeof(canonical),"%llu",(unsigned long long)musicid);
    if(n<=0 || n>=(int)sizeof(canonical)) return ESP_ERR_INVALID_SIZE;
    qqmusic_json_t str_id;
    if(qqmusic_json_member(root,"str_musicid",&str_id)) {
        char value[32];if(!qqmusic_json_string(str_id,value,sizeof(value)) || (value[0] && strcmp(value,canonical))) return ESP_ERR_INVALID_RESPONSE;
    }
    strcpy(account,canonical);
    /* 昵称是可选显示字段；缺少时由页面显示平台账号，不伪造昵称。 */
    qqmusic_json_t nick;
    if(qqmusic_json_member(root,"nick",&nick) && !qqmusic_json_display_string(nick,nickname,64)) { account[0]=nickname[0]=0;return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}
esp_err_t qqmusic_parse_playlists(qqmusic_json_t data,bool favorites,qqmusic_playlists_t *out)
{
    if(!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));qqmusic_json_t list,item;uint64_t total;
    if(!qqmusic_json_member(data,favorites?"v_list":"v_playlist",&list) || list.text[0]!='[' || !number(data,"total",&total)) return ESP_ERR_INVALID_RESPONSE;
    bool more=false;qqmusic_json_t value;
    if(favorites) { uint64_t n;if(!number(data,"hasmore",&n) || n>1) return ESP_ERR_INVALID_RESPONSE;more=n!=0; }
    else if(!qqmusic_json_member(data,"bFinish",&value) || !qqmusic_json_bool(value,&more)) return ESP_ERR_INVALID_RESPONSE;
    else more=!more;
    size_t count=0;
    while(qqmusic_json_index(list,count,&item)) {
        if(count>=QQMUSIC_MAX_PLAYLISTS) { more=true;break; }
        qqmusic_playlist_t *p=&out->items[count];
        if(!number(item,favorites?"id":"tid",&p->id) ||
           !uint32(item,favorites?"dirid":"dirId",&p->dir_id) ||
           !uint32(item,favorites?"songnum":"songNum",&p->total) ||
           !display_text(item,favorites?"title":"dirName",p->name,sizeof(p->name)) ||
           (!p->id && p->dir_id!=201)) { memset(out,0,sizeof(*out));return ESP_ERR_INVALID_RESPONSE; }
        ++count;
    }
    out->count=count;out->has_more=more || total>count;return ESP_OK;
}
static uint64_t hash_uint(uint64_t hash,uint64_t value)
{
    for(unsigned i=0;i<8;i++) { hash^=(unsigned char)(value>>(i*8));hash*=UINT64_C(1099511628211); }
    return hash;
}
esp_err_t qqmusic_parse_tracks(qqmusic_json_t data,uint32_t offset,qqmusic_tracks_t *out)
{
    if(!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));qqmusic_json_t list,item,info;uint64_t changed,mtime;
    if(!qqmusic_json_member(data,"dirinfo",&info) || !uint32(info,"songnum",&out->total) ||
       !number(info,"song_update_time",&changed) || !number(info,"mtime",&mtime) ||
       !qqmusic_json_member(data,"songlist",&list) || list.text[0]!='[') goto invalid;
    out->revision=hash_uint(hash_uint(hash_uint(UINT64_C(14695981039346656037),out->total),changed),mtime);
    size_t count=0;
    while(qqmusic_json_index(list,count,&item)) {
        if(count>=QQMUSIC_MAX_TRACKS) goto invalid;
        qqmusic_track_t *t=&out->items[count];qqmusic_json_t file,album,singers,singer;
        uint32_t seconds;
        if(!text(item,"mid",t->mid,sizeof(t->mid)) || !t->mid[0] ||
           !display_text(item,"name",t->name,sizeof(t->name)) || !uint32(item,"type",&t->song_type) ||
           !uint32(item,"interval",&seconds) || seconds>UINT32_MAX/1000 ||
           !qqmusic_json_member(item,"file",&file) || !text(file,"media_mid",t->media_mid,sizeof(t->media_mid)) ||
           !number(file,"size_128mp3",&t->mp3_bytes) ||
           !qqmusic_json_member(item,"album",&album) || !text(album,"mid",t->album_mid,sizeof(t->album_mid)) ||
           !qqmusic_json_member(item,"singer",&singers) || singers.text[0]!='[') goto invalid;
        t->duration_ms=seconds*1000;
        if(qqmusic_json_index(singers,0,&singer) && !display_text(singer,"name",t->artist,sizeof(t->artist))) goto invalid;
        ++count;
    }
    if(count>UINT32_MAX-offset || (count && (offset>=out->total || count>out->total-offset))) goto invalid;
    out->count=count;out->next_offset=offset+(uint32_t)count;
    out->has_more=out->next_offset<out->total;
    if(!count && out->has_more) goto invalid;
    return ESP_OK;
invalid:
    memset(out,0,sizeof(*out));return ESP_ERR_INVALID_RESPONSE;
}
static bool cdn_url(const char *base,const char *path,char *out,size_t capacity)
{
    const char *host;
    if(!strncmp(base,"https://",8)) host=base+8;
    else if(!strncmp(base,"http://",7)) host=base+7;
    else return false;
    const char *slash=strchr(host,'/');if(!slash || slash[1]) return false;
    size_t length=(size_t)(slash-host),suffix=strlen(".stream.qqmusic.qq.com");
    bool stream_host=length>suffix && !memcmp(slash-suffix,".stream.qqmusic.qq.com",suffix);
    bool dispatch_host=length==strlen("aqqmusic.tc.qq.com") && !memcmp(host,"aqqmusic.tc.qq.com",length);
    if(!stream_host && !dispatch_host) return false;
    for(const char *p=host;p<slash;p++) if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='.' || *p=='-')) return false;
    if(*path=='/') ++path;
    if(strchr(path,'#') || strchr(path,'\r') || strchr(path,'\n')) return false;
    int n=snprintf(out,capacity,"https://%.*s/%s",(int)length,host,path);
    return n>0 && (size_t)n<capacity;
}
esp_err_t qqmusic_parse_audio(qqmusic_json_t data,qqmusic_json_t dispatch,const qqmusic_track_t *track,qqmusic_audio_t *out)
{
    if(!out || !track) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));qqmusic_json_t entries,item,bases,base_item;uint64_t code;
    if(!track->mp3_bytes || !track->mid[0] || !track->media_mid[0]) return QQMUSIC_ERR_UNAVAILABLE;
    if(!qqmusic_json_member(data,"midurlinfo",&entries) || !qqmusic_json_index(entries,0,&item) || !number(item,"result",&code)) return ESP_ERR_INVALID_RESPONSE;
    esp_err_t err=business(code);if(err!=ESP_OK) return err;
    char purl[QQMUSIC_URL_MAX],base[256],filename[80];
    char mid[QQMUSIC_MID_MAX];
    if(!text(item,"songmid",mid,sizeof(mid)) || strcmp(mid,track->mid) || !text(item,"purl",purl,sizeof(purl)) || !purl[0]) return ESP_ERR_INVALID_RESPONSE;
    int n=snprintf(filename,sizeof(filename),"M500%s.mp3",track->media_mid);
    const char *relative=purl[0]=='/'?purl+1:purl;
    if(n<=0 || n>=(int)sizeof(filename) || strncmp(relative,filename,(size_t)n) || (relative[n] && relative[n]!='?')) return QQMUSIC_ERR_UNAVAILABLE;
    if(!number(dispatch,"retcode",&code) || code || !qqmusic_json_member(dispatch,"sip",&bases) ||
       !qqmusic_json_index(bases,0,&base_item) || !qqmusic_json_string(base_item,base,sizeof(base)) ||
       !uint32(data,"expiration",&out->expires_seconds) || !out->expires_seconds ||
       !cdn_url(base,purl,out->url,sizeof(out->url))) { memset(out,0,sizeof(*out));return ESP_ERR_INVALID_RESPONSE; }
    out->full_bytes=track->mp3_bytes;return ESP_OK;
}
