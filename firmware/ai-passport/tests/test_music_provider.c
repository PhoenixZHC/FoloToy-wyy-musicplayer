/* 适配层契约测试；网络替身仅在本主机测试入口链接。 */
#include "music_provider.h"
#include "netease_direct_adapter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned unloaded,qq_closed,qq_restored;
static esp_err_t logout_result;
esp_err_t direct_adapter_init(void) { return ESP_OK; }
esp_err_t direct_adapter_restore(void) { return ESP_OK; }
esp_err_t direct_adapter_logout(void) { return logout_result; }
int64_t direct_adapter_account_id(void) { return 123; }
void netease_direct_session_unload(void) { unloaded++; }
void direct_adapter_nickname(char *out,size_t capacity) { snprintf(out,capacity,"NetEase"); }
esp_err_t direct_adapter_playlists(netease_playlist_list_t *out) { memset(out,0,sizeof(*out));return ESP_OK; }
esp_err_t direct_adapter_tracks(int64_t id,uint32_t limit,uint32_t offset,netease_track_list_t *out)
{ (void)id;(void)limit;(void)offset;memset(out,0,sizeof(*out));return ESP_OK; }
esp_err_t direct_adapter_url(int64_t id,int bitrate,netease_track_url_t *out)
{ (void)bitrate;memset(out,0,sizeof(*out));out->track_id=id;return ESP_OK; }
void qqmusic_client_init(qqmusic_client_t *client) { memset(client,0,sizeof(*client)); }
void qqmusic_client_close(qqmusic_client_t *client) { qq_closed++;memset(client,0,sizeof(*client)); }
esp_err_t qqmusic_client_restore(qqmusic_client_t *client)
{ qq_restored++;strcpy(client->account,"18446744073709551615");strcpy(client->nickname,"QQ");return ESP_OK; }
esp_err_t qqmusic_client_logout(qqmusic_client_t *client) { (void)client;return logout_result; }
esp_err_t qqmusic_client_playlists(qqmusic_client_t *client,qqmusic_playlists_t *out)
{
    (void)client;memset(out,0,sizeof(*out));out->count=1;
    out->items[0].id=0;out->items[0].dir_id=201;out->items[0].total=27;strcpy(out->items[0].name,"喜欢");return ESP_OK;
}
esp_err_t qqmusic_client_tracks(qqmusic_client_t *client,const qqmusic_playlist_t *playlist,uint32_t offset,qqmusic_tracks_t *out)
{
    (void)client;assert(playlist->id==0 && playlist->dir_id==201);assert(offset==8);
    memset(out,0,sizeof(*out));out->count=1;out->total=27;out->next_offset=9;out->has_more=true;out->revision=99;
    strcpy(out->items[0].mid,"004ActualSongMID");strcpy(out->items[0].media_mid,"004ActualMediaMID");
    strcpy(out->items[0].album_mid,"004ActualAlbumMID");out->items[0].song_type=111;
    out->items[0].mp3_bytes=3924526;out->items[0].duration_ms=245000;return ESP_OK;
}
esp_err_t qqmusic_client_audio(qqmusic_client_t *client,const qqmusic_track_t *track,qqmusic_audio_t *out)
{
    (void)client;assert(strcmp(track->mid,"004ActualSongMID")==0);assert(strcmp(track->media_mid,"004ActualMediaMID")==0);
    assert(strcmp(track->album_mid,"004ActualAlbumMID")==0 && track->song_type==111 && track->mp3_bytes==3924526);
    memset(out,0,sizeof(*out));strcpy(out->url,"https://fixture.stream.qqmusic.qq.com/M500004ActualMediaMID.mp3");return ESP_OK;
}
int main(void)
{
    assert(music_provider_open(MUSIC_PLATFORM_QQ)==ESP_OK);assert(unloaded==1 && qq_closed==1);
    assert(music_provider_restore()==ESP_OK && qq_restored==1);
    assert(strcmp(music_provider_account(),"18446744073709551615")==0);
    netease_playlist_list_t lists;netease_track_list_t tracks;netease_track_url_t audio;
    assert(music_provider_playlists(&lists)==ESP_OK && lists.count==1 && lists.items[0].id==0);
    assert(music_provider_tracks(0,8,8,&tracks)==ESP_OK);
    assert(tracks.items[0].id==0 && tracks.total_count==27 && tracks.revision==99 && tracks.metadata_valid);
    assert(tracks.has_next_offset && tracks.next_offset==9);
    assert(music_provider_url(&tracks.items[0],128000,&audio)==ESP_OK && strstr(audio.url,"ActualMediaMID"));
    logout_result=ESP_FAIL;assert(music_provider_logout()==ESP_FAIL);
    assert(music_provider_tracks(0,8,8,&tracks)==ESP_ERR_INVALID_STATE);
    assert(music_provider_open(MUSIC_PLATFORM_NETEASE)==ESP_OK && music_provider_restore()==ESP_OK);
    assert(strcmp(music_provider_account(),"123")==0);
    assert(music_provider_open(MUSIC_PLATFORM_QQ)==ESP_OK);
    assert(music_provider_restore()==ESP_FAIL && qq_restored==1); /* 待退出账号不得恢复旧凭据。 */
    logout_result=ESP_OK;assert(music_provider_restore()==ESP_ERR_NOT_FOUND && qq_restored==1);
    music_provider_close();puts("Music provider: PASS");return 0;
}
