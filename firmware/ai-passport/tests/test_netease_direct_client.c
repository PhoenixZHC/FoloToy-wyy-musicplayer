#include "netease_direct_client.h"
#include "direct_stream.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void test_qr(void)
{
    netease_direct_qr_t qr;
    assert(netease_direct_parse_qr_key("{\"code\":200,\"unikey\":\"abc123\"}", &qr) == ESP_OK);
    assert(strcmp(qr.key, "abc123") == 0);
    assert(strstr(qr.payload, "abc123") != NULL);

    netease_direct_qr_status_t status;
    assert(netease_direct_parse_qr_status("{\"code\":803,\"profile\":{\"nickname\":\"A\"}}", &status) == ESP_OK);
    assert(status.state == NETEASE_DIRECT_QR_CONFIRMED);
    assert(strcmp(status.nickname, "A") == 0);
    int64_t user_id = 0;
    assert(netease_direct_parse_qr_user_id("{\"code\":200,\"profile\":{\"userId\":123456}}", &user_id) == ESP_OK);
    assert(user_id == 123456);
    assert(netease_direct_parse_qr_user_id("{\"code\":200,\"profile\":{}}", &user_id) == ESP_ERR_INVALID_RESPONSE);
}

static void test_lists_and_url(void)
{
    netease_direct_playlist_list_t playlists;
    assert(netease_direct_parse_playlists("{\"code\":200,\"playlist\":[{\"id\":12,\"name\":\"收藏\",\"trackCount\":3}]}", &playlists) == ESP_OK);
    assert(playlists.count == 1 && playlists.items[0].id == 12);

    netease_direct_track_list_t tracks;
    assert(netease_direct_parse_tracks("{\"code\":200,\"songs\":[{\"id\":9,\"name\":\"歌\",\"dt\":1234,\"ar\":[{\"name\":\"人\"}]}]}", 12, &tracks) == ESP_OK);
    assert(tracks.count == 1 && tracks.items[0].duration_ms == 1234);

    netease_direct_track_url_t url;
    assert(netease_direct_parse_track_url("{\"code\":200,\"data\":[{\"id\":9,\"url\":null,\"code\":404}]}", &url)==NETEASE_DIRECT_ERR_UNAVAILABLE);
    assert(netease_direct_parse_track_url("{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https://cdn.test/a.mp3\",\"expi\":900}]}", &url) == ESP_OK);
    assert(strcmp(url.format, "mp3") == 0 && url.expires_in_seconds == 900);
    const char *bad_urls[] = {
        "{\"data\":[]}",
        "{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https:///a.mp3\",\"expi\":900}]}",
        "{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https://cdn.test/a.mp3\",\"expi\":900,\"freeTrialInfo\":{\"start\":0}}]}",
        "{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https://user@cdn.test/a.mp3\",\"expi\":900}]}",
        "{\"code\":200,\"data\":[{\"id\":9,\"url\":\"https://cdn.test/a.mp3\",\"expi\":4294968196}]}"
    };
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); ++i) {
        assert(netease_direct_parse_track_url(bad_urls[i], &url) != ESP_OK);
        assert(url.url[0] == 0);
    }
    assert(netease_direct_parse_playlists("{\"code\":200,\"playlist\":[{\"id\":12,\"name\":\"A\",\"trackCount\":4294967296}]}", &playlists) != ESP_OK);
}

static void test_cookie_and_bodies(void)
{
    char cookie[NETEASE_DIRECT_COOKIE_MAX_BYTES + 1];
    assert(netease_direct_cookie_copy("MUSIC_U=abc; __csrf=def", cookie, sizeof(cookie)) == ESP_OK);
    assert(netease_direct_cookie_has_login(cookie));
    assert(!netease_direct_cookie_has_login("__csrf=def"));
    assert(!netease_direct_cookie_has_login("NOT_MUSIC_U=abc"));
    assert(!netease_direct_cookie_has_login("MUSIC_U="));
    assert(!netease_direct_cookie_has_login("MUSIC_A=anonymous"));
    assert(netease_direct_cookie_copy("MUSIC_U=x\r\nInjected: yes", cookie, sizeof(cookie)) != ESP_OK);

    char body[4096];
    char *payload = netease_direct_session_json("{}", "MUSIC_U=abc; __csrf=def", false);
    assert(payload && strcmp(payload, "{\"csrf_token\":\"def\"}") == 0); free(payload);
    payload = netease_direct_session_json("{\"header\":{\"os\":\"pc\"}}", "NOT_MUSIC_U=bad; MUSIC_U=abc; __csrf=def", true);
    assert(payload && strcmp(payload, "{\"header\":{\"os\":\"pc\",\"__csrf\":\"def\",\"MUSIC_U\":\"abc\"}}") == 0); free(payload);
    assert(netease_direct_build_weapi_body("{}", body, sizeof(body)) == ESP_OK);
    assert(strcmp(body, "params=kxBNj%2Fl03n7aQ%2Fq0EdABwtOsIYuwYGkESNTUk4QvWCg%3D&encSecKey=35701388baf89fed412e11269b9c76625d095ecaf17f03fa018abe19ea2d38b949debf242ee39a71ca1f6cda71b1b86a45aa909ee27f7e78e267d34e732f0de948206c3340a788d0003372183e2f753c1f78b66ac23d134ac1fc9b993156520ea826b8aa89a962d4491b4b8d7e08738e1da9b07aa39bf4a7ef0b1c210728cd52") == 0);
    assert(netease_direct_build_eapi_body("/api/song/enhance/player/url", "{}", body, sizeof(body)) == ESP_OK);
    assert(strcmp(body, "params=FA90B329E9614F79E79598F37DC2EDB430F8378D2A2796338F0BFDEAEF824A224587DAF9547EA41FDE03F09C4DB4406257EF0A7902CA8133F62C9CA015A2C4E92359D9E85A016CCDA85D16477A95BBA67B89F90D47172660CD99FBBCB8C91C15") == 0);
    assert(netease_direct_build_eapi_body("/api/song/enhance/player/url", "{}", body, 8) == ESP_ERR_INVALID_SIZE);
    char authenticated[1800];
    memset(authenticated, 'a', sizeof(authenticated));
    memcpy(authenticated, "{\"MUSIC_U\":\"", 12);
    memcpy(authenticated + sizeof(authenticated) - 3, "\"}", 3);
    assert(netease_direct_build_eapi_body("/api/song/enhance/player/url", authenticated, body, sizeof(body)) == ESP_OK);
    strcpy(cookie, "MUSIC_U=old; __csrf=x");
    assert(netease_direct_cookie_merge(cookie, sizeof(cookie), "MUSIC_U=new; Path=/; HttpOnly") == ESP_OK);
    assert(strcmp(cookie, "__csrf=x; MUSIC_U=new") == 0);
    assert(netease_direct_cookie_merge(cookie, sizeof(cookie), "MUSIC_U=; Max-Age=0") == ESP_OK);
    assert(!netease_direct_cookie_has_login(cookie));
    char saved[128]; strcpy(saved, cookie);
    assert(netease_direct_cookie_merge(cookie, 8, "bad=value; Path=/") != ESP_OK);
    assert(strcmp(cookie, saved) == 0);
}

static void test_stream(void)
{
    const char *json = "{\"playlist\":{\"name\":\"trackIds [ ignored\",\"tracks\":[{\"id\":999}],\"trackIds\":[{\"id\":1},{\"id\":2},{\"id\":3},{\"id\":4}]},\"code\":200}";
    for (size_t chunk = 1; chunk < 32; ++chunk) {
        direct_stream_t s; direct_stream_init(&s, DIRECT_STREAM_IDS, NULL, 1, 2);
        size_t n = strlen(json);
        for (size_t i = 0; i < n; i += chunk) assert(direct_stream_feed(&s, json + i, n - i < chunk ? n - i : chunk) == ESP_OK);
        assert(direct_stream_finish(&s) == ESP_OK);
        assert(s.total == 4 && s.id_count == 2 && s.ids[0] == 2 && s.ids[1] == 3);
    }
    direct_stream_t s; netease_direct_playlist_list_t p;
    direct_stream_init(&s, DIRECT_STREAM_PLAYLISTS, &p, 0, 20);
    json = "{\"code\":200,\"playlist\":[{\"id\":12,\"name\":\"A \\u4e2d\",\"trackCount\":2,\"creator\":{\"id\":999,\"nickname\":\"other\"}}]}";
    assert(direct_stream_feed(&s, json, strlen(json)) == ESP_OK);
    assert(direct_stream_finish(&s) == ESP_OK);
    assert(p.count == 1 && p.items[0].id == 12 && strcmp(p.items[0].name, "A 中") == 0);
    direct_stream_init(&s, DIRECT_STREAM_PLAYLISTS, &p, 0, 20);
    json = "{\"code\":200,\"playlist\":[";
    assert(direct_stream_feed(&s, json, strlen(json)) == ESP_OK);
    for (unsigned i = 0; i < 21; ++i) {
        char item[100];
        snprintf(item, sizeof(item), "%s{\"id\":%u,\"name\":\"A\",\"trackCount\":2}", i ? "," : "", i + 1);
        assert(direct_stream_feed(&s, item, strlen(item)) == ESP_OK);
    }
    assert(direct_stream_feed(&s, "]}", 2) == ESP_OK && direct_stream_finish(&s) == ESP_OK);
    assert(p.count == 20 && p.truncated && p.items[19].id == 20);
    /* An overflowing list must still reject malformed data after the cap. */
    direct_stream_init(&s, DIRECT_STREAM_PLAYLISTS, &p, 0, 20);
    json = "{\"code\":200,\"playlist\":[";
    assert(direct_stream_feed(&s, json, strlen(json)) == ESP_OK);
    for (unsigned i = 0; i < 21; ++i) {
        const char *item = i ? ",{\"id\":1,\"name\":\"A\",\"trackCount\":2}" : "{\"id\":1,\"name\":\"A\",\"trackCount\":2}";
        assert(direct_stream_feed(&s, item, strlen(item)) == ESP_OK);
    }
    assert(direct_stream_feed(&s, ",null]}", 7) == ESP_ERR_INVALID_RESPONSE);
    assert(direct_stream_finish(&s) != ESP_OK && p.count == 0 && !p.truncated);
    netease_direct_track_list_t tracks; memset(&tracks, 0, sizeof(tracks));
    direct_stream_init(&s, DIRECT_STREAM_SONGS, NULL, 0, 15); s.tracks = &tracks;
    json = "{\"code\":200,\"songs\":[{\"id\":9,\"name\":\"song\",\"dt\":1000,\"ar\":[{\"id\":888,\"name\":\"artist\"}],\"al\":{\"id\":777,\"name\":\"album\"}}]}";
    for (size_t i = 0; i < strlen(json); ++i) assert(direct_stream_feed(&s, json + i, 1) == ESP_OK);
    assert(direct_stream_finish(&s) == ESP_OK);
    assert(tracks.count == 1 && tracks.items[0].id == 9 && strcmp(tracks.items[0].artist, "artist") == 0 && strcmp(tracks.items[0].name, "song") == 0);
    const char *bad[] = {"{\"code\":200,\"playlist\":{\"trackIds\":[{\"id\":1},]}}",
        "{\"code\":301,\"playlist\":{\"trackIds\":[]}}",
        "{\"code\":200,\"playlist\":{\"trackIds\":[{\"id\":-1}]}}",
        "{\"code\":200,\"playlist\":{\"trackIds\":[{\"id\":01}]}}",
        "{\"code\":200,\"code\":{},\"playlist\":{\"trackIds\":[]}}",
        "{\"code\":200,\"playlist\":{\"trackIds\":[]}}junk",
        "{\"code\":200,\"playlist\":{\"trackIds\":[{\"id\":1}]"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        direct_stream_init(&s, DIRECT_STREAM_IDS, NULL, 0, 15);
        (void)direct_stream_feed(&s, bad[i], strlen(bad[i]));
        assert(direct_stream_finish(&s) != ESP_OK);
    }
    direct_stream_init(&s, DIRECT_STREAM_PLAYLISTS, &p, 0, 20);
    json = "{\"code\":200,\"playlist\":[{\"id\":12,\"name\":\"A\",\"trackCount\":2,\"description\":\"";
    assert(direct_stream_feed(&s, json, strlen(json)) == ESP_OK);
    char padding[512]; memset(padding, 'x', sizeof(padding));
    for (unsigned i = 0; i < 1000; ++i) assert(direct_stream_feed(&s, padding, sizeof(padding)) == ESP_OK);
    json = "\"}]}"; assert(direct_stream_feed(&s, json, strlen(json)) == ESP_OK);
    assert(direct_stream_finish(&s) == ESP_OK && p.count == 1);
    /* Deterministic malformed-input sweep: exercise chunk boundaries and nesting. */
    unsigned seed = 1;
    for (unsigned trial = 0; trial < 1000; ++trial) {
        direct_stream_init(&s, DIRECT_STREAM_IDS, NULL, 0, 15);
        char bytes[64];
        for (size_t i = 0; i < sizeof(bytes); ++i) { seed = seed * 1664525U + 1013904223U; bytes[i] = (char)(seed >> 24); }
        (void)direct_stream_feed(&s, bytes, sizeof(bytes)); (void)direct_stream_finish(&s);
    }
}

int main(void)
{
    test_qr();
    test_lists_and_url();
    test_cookie_and_bodies();
    test_stream();
    puts("netease_direct_client: PASS");
    return 0;
}
