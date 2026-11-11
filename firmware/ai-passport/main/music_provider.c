#include "music_provider.h"
#include "netease_direct_adapter.h"
#include "nvs.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static music_platform_t s_platform;
static qqmusic_client_t s_qq;
/* Names already live in the UI list; retain only real identifiers for paging. */
static struct { uint64_t id;uint32_t dir_id; } s_qq_lists[QQMUSIC_MAX_PLAYLISTS];
static size_t s_qq_list_count;
static char s_account[32];
static bool s_blocked;
static bool s_logout_pending[2];
static void text(char *out,size_t capacity,const char *in)
{
    if(!capacity)return;
    size_t n=strlen(in);if(n>=capacity)n=capacity-1;
    while(n && ((unsigned char)in[n]&0xc0)==0x80)n--;
    memcpy(out,in,n);out[n]=0;
}
static esp_err_t mapped(esp_err_t err)
{
    if(err==QQMUSIC_ERR_AUTH)return NETEASE_DIRECT_ERR_AUTH;
    if(err==QQMUSIC_ERR_UNAVAILABLE)return NETEASE_DIRECT_ERR_UNAVAILABLE;
    return err;
}
music_platform_t music_provider_platform(void) { return s_platform; }
const char *music_provider_name(void) { return s_platform==MUSIC_PLATFORM_QQ?"QQ 音乐":"网易云音乐"; }
const char *music_provider_account(void)
{
    if(s_platform==MUSIC_PLATFORM_QQ)return s_qq.account;
    snprintf(s_account,sizeof(s_account),"%" PRId64,direct_adapter_account_id());return s_account;
}
qqmusic_client_t *music_provider_qq_client(void) { return &s_qq; }
void music_provider_close(void)
{
    qqmusic_client_close(&s_qq);memset(s_qq_lists,0,sizeof(s_qq_lists));s_qq_list_count=0;
    netease_direct_session_unload();s_account[0]=0;s_blocked=false;
}
esp_err_t music_provider_open(music_platform_t platform)
{
    music_provider_close();s_platform=platform;
    if(platform==MUSIC_PLATFORM_QQ) { qqmusic_client_init(&s_qq);return ESP_OK; }
    return direct_adapter_init();
}
esp_err_t music_provider_restore(void)
{
    if(s_logout_pending[s_platform]) {
        esp_err_t err=music_provider_logout();
        return err==ESP_OK?ESP_ERR_NOT_FOUND:err;
    }
    if(s_platform==MUSIC_PLATFORM_NETEASE)return direct_adapter_restore();
    esp_err_t err=qqmusic_client_restore(&s_qq);
    return err==ESP_ERR_NVS_NOT_FOUND?ESP_ERR_NOT_FOUND:mapped(err);
}
esp_err_t music_provider_logout(void)
{
    s_blocked=true;
    s_logout_pending[s_platform]=true;
    esp_err_t err=s_platform==MUSIC_PLATFORM_QQ?mapped(qqmusic_client_logout(&s_qq)):direct_adapter_logout();
    if(err==ESP_OK) { s_logout_pending[s_platform]=false;s_blocked=false; }
    return err;
}
void music_provider_nickname(char *out,size_t capacity)
{
    if(s_platform==MUSIC_PLATFORM_QQ)text(out,capacity,s_qq.nickname);
    else direct_adapter_nickname(out,capacity);
}
esp_err_t music_provider_playlists(netease_playlist_list_t *out)
{
    if(s_blocked)return ESP_ERR_INVALID_STATE;
    if(s_platform==MUSIC_PLATFORM_NETEASE)return direct_adapter_playlists(out);
    qqmusic_playlists_t *lists=calloc(1,sizeof(*lists));
    if(!lists)return ESP_ERR_NO_MEM;
    esp_err_t err=qqmusic_client_playlists(&s_qq,lists);memset(out,0,sizeof(*out));
    s_qq_list_count=0;
    if(err!=ESP_OK) { free(lists);return mapped(err); }
    out->count=lists->count;out->truncated=lists->has_more;
    for(size_t i=0;i<out->count;i++) {
        const qqmusic_playlist_t *p=&lists->items[i];
        if(p->id>INT64_MAX || p->total>INT32_MAX) { free(lists);return ESP_ERR_INVALID_SIZE; }
        s_qq_lists[i].id=p->id;s_qq_lists[i].dir_id=p->dir_id;
        out->items[i].id=(int64_t)p->id;out->items[i].track_count=(int)p->total;
        text(out->items[i].name,sizeof(out->items[i].name),p->name);
    }
    s_qq_list_count=lists->count;free(lists);return ESP_OK;
}
esp_err_t music_provider_tracks(int64_t id,uint32_t limit,uint32_t offset,netease_track_list_t *out)
{
    if(s_blocked)return ESP_ERR_INVALID_STATE;
    if(s_platform==MUSIC_PLATFORM_NETEASE)return direct_adapter_tracks(id,limit,offset,out);
    if(limit!=QQMUSIC_MAX_TRACKS || !s_qq_list_count)return ESP_ERR_INVALID_STATE;
    qqmusic_playlist_t playlist={0};bool found=false;
    for(size_t i=0;i<s_qq_list_count;i++)if(s_qq_lists[i].id==(uint64_t)id) {
        playlist.id=s_qq_lists[i].id;playlist.dir_id=s_qq_lists[i].dir_id;found=true;break;
    }
    if(!found)return ESP_ERR_NOT_FOUND;
    qqmusic_tracks_t *page=calloc(1,sizeof(*page));if(!page)return ESP_ERR_NO_MEM;
    esp_err_t err=qqmusic_client_tracks(&s_qq,&playlist,offset,page);memset(out,0,sizeof(*out));
    if(err==ESP_OK && page->next_offset>INT32_MAX)err=ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK) {
        out->playlist_id=id;out->count=page->count;out->has_next_offset=page->has_more;
        out->next_offset=(int)page->next_offset;out->total_count=page->total;
        out->revision=page->revision;out->metadata_valid=true;
        for(size_t i=0;i<page->count;i++) {
            const qqmusic_track_t *p=&page->items[i];netease_track_t *t=&out->items[i];
            text(t->name,sizeof(t->name),p->name);text(t->artist,sizeof(t->artist),p->artist);
            t->duration_ms=(int)p->duration_ms;
            memcpy(t->qq_mid,p->mid,sizeof(t->qq_mid));memcpy(t->qq_media_mid,p->media_mid,sizeof(t->qq_media_mid));
            memcpy(t->qq_album_mid,p->album_mid,sizeof(t->qq_album_mid));t->qq_song_type=p->song_type;t->qq_mp3_bytes=p->mp3_bytes;
        }
    }
    free(page);return mapped(err);
}
esp_err_t music_provider_url(const netease_track_t *track,int bitrate,netease_track_url_t *out)
{
    if(s_blocked)return ESP_ERR_INVALID_STATE;
    if(s_platform==MUSIC_PLATFORM_NETEASE)return direct_adapter_url(track->id,bitrate,out);
    qqmusic_track_t source={0};
    memcpy(source.mid,track->qq_mid,sizeof(source.mid));memcpy(source.media_mid,track->qq_media_mid,sizeof(source.media_mid));
    memcpy(source.album_mid,track->qq_album_mid,sizeof(source.album_mid));source.song_type=track->qq_song_type;
    source.duration_ms=(uint32_t)track->duration_ms;source.mp3_bytes=track->qq_mp3_bytes;
    qqmusic_audio_t *audio=calloc(1,sizeof(*audio));if(!audio)return ESP_ERR_NO_MEM;
    esp_err_t err=qqmusic_client_audio(&s_qq,&source,audio);memset(out,0,sizeof(*out));
    if(err==ESP_OK) {
        if(strlen(audio->url)>=sizeof(out->url))err=ESP_ERR_INVALID_SIZE;
        else { strcpy(out->url,audio->url);strcpy(out->format,"mp3");out->expires_in_seconds=(int)audio->expires_seconds; }
    }
    memset(audio,0,sizeof(*audio));free(audio);return mapped(err);
}
esp_err_t music_provider_cover_url(const netease_track_t *track,char *out,size_t capacity)
{
    if(out && capacity)out[0]=0;
    if(!track || !out || !capacity)return ESP_ERR_INVALID_ARG;
    if(s_blocked)return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    if(s_platform==MUSIC_PLATFORM_NETEASE)err=netease_direct_get_cover_url(track->id,out,capacity);
    else {
        qqmusic_track_t source={0};memcpy(source.album_mid,track->qq_album_mid,sizeof(source.album_mid));
        err=qqmusic_cover_url(&source,out,capacity);
    }
    if(err!=ESP_OK)out[0]=0;
    return mapped(err);
}
esp_err_t music_provider_lyrics(const netease_track_t *track,char *out,size_t capacity)
{
    if(out && capacity)out[0]=0;
    if(!track || !out || !capacity)return ESP_ERR_INVALID_ARG;
    if(s_blocked)return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    if(s_platform==MUSIC_PLATFORM_NETEASE)err=netease_direct_get_lyrics(track->id,out,capacity);
    else {
        qqmusic_track_t source={0};memcpy(source.mid,track->qq_mid,sizeof(source.mid));
        err=qqmusic_client_lyrics(&s_qq,&source,out,capacity);
    }
    if(err!=ESP_OK)out[0]=0;
    return mapped(err);
}
