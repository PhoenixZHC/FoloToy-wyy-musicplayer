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
    puts("qqmusic_parse: 5 groups passed");return 0;
}
