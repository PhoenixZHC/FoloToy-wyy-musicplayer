#include "qqmusic_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static qqmusic_json_t json(const char *s)
{
    qqmusic_json_t out;assert(qqmusic_json_parse(s,strlen(s),&out));return out;
}
int main(void)
{
    char account[32],nick[64];
    const char *credential="{\"musicid\":18446744073709551615,\"str_musicid\":\"18446744073709551615\",\"loginType\":6,\"musickey\":\"isolated-test\"}";
    assert(qqmusic_parse_credential(credential,strlen(credential),account,nick)==ESP_OK);
    assert(!strcmp(account,"18446744073709551615") && !nick[0]);
    assert(qqmusic_parse_credential("{}",2,account,nick)!=ESP_OK && !account[0]);
    const char *mismatch="{\"musicid\":123,\"str_musicid\":\"124\",\"loginType\":6,\"musickey\":\"test\"}";
    assert(qqmusic_parse_credential(mismatch,strlen(mismatch),account,nick)!=ESP_OK);
    qqmusic_json_t data;
    const char *expired="{\"code\":0,\"req_0\":{\"code\":104401}}";
    assert(qqmusic_parse_response(expired,strlen(expired),&data)==QQMUSIC_ERR_AUTH);
    const char *rejected="{\"code\":0,\"req_0\":{\"code\":104604}}";
    assert(qqmusic_parse_response(rejected,strlen(rejected),&data)==QQMUSIC_ERR_REJECTED);
    qqmusic_playlists_t lists;
    assert(qqmusic_parse_playlists(json("{\"total\":1,\"bFinish\":true,\"v_playlist\":[{\"tid\":0,\"dirId\":201,\"songNum\":3,\"dirName\":\"喜欢\"}]}"),false,&lists)==ESP_OK);
    assert(lists.count==1 && !lists.has_more && lists.items[0].dir_id==201 && lists.items[0].id==0);
    qqmusic_tracks_t tracks;
    assert(qqmusic_parse_tracks(json("{\"dirinfo\":{\"songnum\":2,\"mtime\":10,\"song_update_time\":11},\"songlist\":[{\"mid\":\"00opaque\",\"name\":\"测试\",\"type\":0,\"interval\":210,\"album\":{\"mid\":\"00album\"},\"singer\":[{\"name\":\"歌手\"}],\"file\":{\"media_mid\":\"00media\",\"size_128mp3\":1234567}}]}"),0,&tracks)==ESP_OK);
    assert(tracks.count==1 && tracks.total==2 && tracks.has_more && tracks.next_offset==1);
    assert(!strcmp(tracks.items[0].mid,"00opaque") && tracks.items[0].duration_ms==210000);
    qqmusic_audio_t audio;
    qqmusic_json_t dispatch=json("{\"retcode\":0,\"sip\":[\"http://isure.stream.qqmusic.qq.com/\"]}");
    qqmusic_json_t urls=json("{\"expiration\":600,\"sip\":[],\"midurlinfo\":[{\"result\":0,\"songmid\":\"00opaque\",\"purl\":\"M50000media.mp3?vkey=isolated-test\"}]}");
    assert(qqmusic_parse_audio(urls,dispatch,&tracks.items[0],&audio)==ESP_OK);
    assert(!strcmp(audio.url,"https://isure.stream.qqmusic.qq.com/M50000media.mp3?vkey=isolated-test") && audio.full_bytes==1234567);
    assert(qqmusic_parse_audio(urls,json("{\"retcode\":0,\"sip\":[\"http://aqqmusic.tc.qq.com/\"]}"),&tracks.items[0],&audio)==ESP_OK);
    qqmusic_json_t denied=json("{\"midurlinfo\":[{\"result\":104003}]}");
    assert(qqmusic_parse_audio(denied,dispatch,&tracks.items[0],&audio)==QQMUSIC_ERR_UNAVAILABLE && !audio.url[0]);
    assert(qqmusic_parse_audio(urls,json("{\"retcode\":0,\"sip\":[\"https://isure.stream.qqmusic.qq.com.attacker.org/\"]}"),&tracks.items[0],&audio)!=ESP_OK);
    tracks.items[0].mp3_bytes=0;
    assert(qqmusic_parse_audio(urls,dispatch,&tracks.items[0],&audio)==QQMUSIC_ERR_UNAVAILABLE);
    qqmusic_track_t media={0};strcpy(media.mid,"0039MnYb0qxYhV");strcpy(media.album_mid,"000MkMni19ClKG");
    char url[256],lyrics[128];
    assert(qqmusic_cover_url(&media,url,sizeof(url))==ESP_OK && !strcmp(url,"https://y.gtimg.cn/music/photo_new/T002R90x90M000000MkMni19ClKG.jpg"));
    assert(qqmusic_lyrics_url(&media,url,sizeof(url))==ESP_OK && strstr(url,"songmid=0039MnYb0qxYhV&format=json&nobase64=1"));
    assert(qqmusic_cover_url(&media,url,8)==ESP_ERR_INVALID_SIZE && !url[0]);
    strcpy(media.album_mid,"../bad");assert(qqmusic_cover_url(&media,url,sizeof(url))==ESP_ERR_INVALID_RESPONSE && !url[0]);
    media.album_mid[0]=0;assert(qqmusic_cover_url(&media,url,sizeof(url))==ESP_ERR_NOT_FOUND && !url[0]);
    memset(media.mid,'a',sizeof(media.mid));assert(qqmusic_lyrics_url(&media,url,sizeof(url))==ESP_ERR_INVALID_RESPONSE && !url[0]);
    const char *lrc="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":\"[00:01.00][00:02.00]晴天\\n\\u6d4b\\u8bd5\"}";
    assert(qqmusic_parse_lyrics(lrc,strlen(lrc),lyrics,sizeof(lyrics))==ESP_OK && !strcmp(lyrics,"[00:01.00][00:02.00]晴天\n测试"));
    assert(qqmusic_parse_lyrics(lrc,strlen(lrc),lyrics,4)==ESP_ERR_INVALID_SIZE && !lyrics[0]);
    const char *empty="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":\"\"}";
    assert(qqmusic_parse_lyrics(empty,strlen(empty),lyrics,sizeof(lyrics))==ESP_ERR_NOT_FOUND && !lyrics[0]);
    const char *plain="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":\"无时间轴文本\"}";
    assert(qqmusic_parse_lyrics(plain,strlen(plain),lyrics,sizeof(lyrics))==ESP_OK && !strcmp(lyrics,"无时间轴文本"));
    const char *bad="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":9}";
    assert(qqmusic_parse_lyrics(bad,strlen(bad),lyrics,sizeof(lyrics))==ESP_ERR_INVALID_RESPONSE && !lyrics[0]);
    const char *auth="{\"retcode\":1000,\"code\":0,\"subcode\":0}";
    assert(qqmusic_parse_lyrics(auth,strlen(auth),lyrics,sizeof(lyrics))==QQMUSIC_ERR_AUTH && !lyrics[0]);
    assert(qqmusic_parse_lyrics(lrc,QQMUSIC_LYRICS_RESPONSE_MAX+1,lyrics,sizeof(lyrics))==ESP_ERR_INVALID_SIZE && !lyrics[0]);
    char long_json[9000],long_out[9000];
    const char *prefix="{\"retcode\":0,\"code\":0,\"subcode\":0,\"lyric\":\"";
    size_t prefix_bytes=strlen(prefix);memcpy(long_json,prefix,prefix_bytes);
    memset(long_json+prefix_bytes,'x',QQMUSIC_LYRICS_MAX_BYTES+1);
    strcpy(long_json+prefix_bytes+QQMUSIC_LYRICS_MAX_BYTES+1,"\"}");
    assert(qqmusic_parse_lyrics(long_json,strlen(long_json),long_out,sizeof(long_out))==ESP_ERR_INVALID_SIZE && !long_out[0]);
    puts("qqmusic_parse: 7 groups passed");return 0;
}
