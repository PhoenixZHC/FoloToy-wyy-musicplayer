#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "netease_client.h"

int main(void)
{
    const char *qr_json =
        "{\"login_id\":\"login-1\",\"qr_payload\":\"https://music.163.com/login\",\"expires_in_seconds\":180}";
    netease_qr_start_t qr = {0};

    assert(netease_parse_qr_start(qr_json, &qr) == ESP_OK);
    assert(strcmp(qr.login_id, "login-1") == 0);
    assert(strcmp(qr.qr_payload, "https://music.163.com/login") == 0);
    assert(qr.expires_in_seconds == 180);

    const char *status_json =
        "{\"state\":\"confirmed\",\"netease_user_id\":123,\"nickname\":\"tester\"}";
    netease_qr_status_t status = {0};

    assert(netease_parse_qr_status(status_json, &status) == ESP_OK);
    assert(status.state == NETEASE_QR_CONFIRMED);
    assert(status.netease_user_id == 123);
    assert(strcmp(status.nickname, "tester") == 0);

    const char *playlists_json =
        "{\"playlists\":[{\"id\":1,\"name\":\"Liked\",\"track_count\":2,\"owner_name\":\"me\"}]}";
    netease_playlist_list_t playlists = {0};

    assert(netease_parse_playlists(playlists_json, &playlists) == ESP_OK);
    assert(playlists.count == 1);
    assert(playlists.items[0].id == 1);
    assert(strcmp(playlists.items[0].name, "Liked") == 0);
    assert(playlists.items[0].track_count == 2);
    assert(strcmp(playlists.items[0].owner_name, "me") == 0);

    const char *tracks_json =
        "{\"playlist_id\":1,\"tracks\":[{\"id\":9,\"name\":\"Song\",\"artist\":\"Artist\",\"duration_ms\":210000}],\"next_offset\":30}";
    netease_track_list_t tracks = {0};

    assert(netease_parse_tracks(tracks_json, &tracks) == ESP_OK);
    assert(tracks.playlist_id == 1);
    assert(tracks.count == 1);
    assert(tracks.items[0].id == 9);
    assert(strcmp(tracks.items[0].artist, "Artist") == 0);
    assert(tracks.next_offset == 30);

    char oversized_tracks_json[4096] = {0};
    size_t used = (size_t)snprintf(oversized_tracks_json,
                                   sizeof(oversized_tracks_json),
                                   "{\"playlist_id\":1,\"tracks\":[");
    for (int i = 0; i < 16; i++) {
        used += (size_t)snprintf(oversized_tracks_json + used,
                                 sizeof(oversized_tracks_json) - used,
                                 "%s{\"id\":%d,\"name\":\"Song\",\"artist\":\"Artist\",\"duration_ms\":1}",
                                 i == 0 ? "" : ",",
                                 i + 1);
    }
    snprintf(oversized_tracks_json + used,
             sizeof(oversized_tracks_json) - used,
             "]}");
    assert(netease_parse_tracks(oversized_tracks_json, &tracks) == ESP_OK);
    assert(tracks.count == 15);

    const char *unavailable_json =
        "{\"error\":\"track_unavailable\",\"message\":\"这首歌当前无法播放\"}";
    netease_track_url_t track_url = {0};

    assert(netease_parse_track_url(unavailable_json, &track_url) == ESP_ERR_NOT_FOUND);
    assert(netease_parse_qr_start("{\"login_id\":\"a\",\"qr_payload\":\"b\",\"expires_in_seconds\":1.5}", &qr) == ESP_ERR_INVALID_RESPONSE);
    assert(netease_parse_playlists("{\"playlists\":[{\"id\":-1,\"name\":\"bad\",\"track_count\":2,\"owner_name\":\"me\"}]}", &playlists) == ESP_ERR_INVALID_RESPONSE);
    assert(netease_parse_tracks("{\"playlist_id\":1,\"tracks\":[{\"id\":9,\"name\":\"Song\",\"artist\":\"Artist\",\"duration_ms\":-10}]}", &tracks) == ESP_ERR_INVALID_RESPONSE);
    return 0;
}
