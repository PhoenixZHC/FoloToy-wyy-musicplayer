#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "netease_official_client.h"

#ifdef NETEASE_OFFICIAL_HOST_TEST
typedef struct {
    const char *path;
    const char *response;
    int http_status;
} fixture_t;

static fixture_t g_fixtures[8];
static int g_fixture_count;
static int g_fixture_index;
static char g_last_query[4096];

static esp_err_t fixture_transport(const netease_official_client_t *client,
                                   const char *path,
                                   esp_http_client_method_t method,
                                   const char *encoded_query,
                                   char *response,
                                   size_t response_size,
                                   int *http_status)
{
    (void)client;
    assert(method == HTTP_METHOD_POST);
    assert(g_fixture_index < g_fixture_count);
    assert(strcmp(path, g_fixtures[g_fixture_index].path) == 0);
    snprintf(g_last_query, sizeof(g_last_query), "%s", encoded_query);
    snprintf(response, response_size, "%s", g_fixtures[g_fixture_index].response);
    *http_status = g_fixtures[g_fixture_index].http_status;
    g_fixture_index++;
    return ESP_OK;
}

static netease_official_client_t make_client(void)
{
    netease_official_config_t config = {
        .app_id = "app-test",
        .app_secret = "secret-test",
        .private_key_pem = "-----BEGIN PRIVATE KEY-----\ntest-only\n-----END PRIVATE KEY-----\n",
        .device_id = "device-test",
        .app_version = "1.0.0",
        .timeout_ms = 10000,
    };
    netease_official_client_t client = {0};
    assert(netease_official_init(&client, &config) == ESP_OK);
    netease_official_set_test_transport(&client, fixture_transport);
    return client;
}

static void reset_fixtures(void)
{
    memset(g_fixtures, 0, sizeof(g_fixtures));
    g_fixture_count = 0;
    g_fixture_index = 0;
    g_last_query[0] = '\0';
}

static void push_fixture(const char *path, const char *response)
{
    assert(g_fixture_count < (int)(sizeof(g_fixtures) / sizeof(g_fixtures[0])));
    g_fixtures[g_fixture_count].path = path;
    g_fixtures[g_fixture_count].response = response;
    g_fixtures[g_fixture_count].http_status = 200;
    g_fixture_count++;
}

static void test_anonymous_login_builds_clientid_and_reads_token(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_ANONYMOUS_LOGIN,
                 "{\"code\":200,\"data\":{\"accessToken\":\"anon-token\",\"refreshToken\":\"ignore\",\"expireTime\":604800},\"message\":\"\"}");
    netease_official_client_t client = make_client();
    char token[NETEASE_OFFICIAL_TOKEN_LEN] = {0};
	    assert(netease_official_anonymous_login(&client, token, sizeof(token)) == ESP_OK);
	    assert(strcmp(token, "anon-token") == 0);
	    assert(strstr(g_last_query, "appSecret=secret-test") != NULL);
	    assert(strstr(g_last_query, "bizContent=%7B%22clientId%22%3A%22app-test%22%7D") != NULL);
	}

static void test_qr_start_uses_type_and_expiry_and_reads_url_key(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_QR_START,
                 "{\"code\":200,\"subCode\":null,\"message\":null,\"data\":{\"qrCodeUrl\":\"https://163cn.tv/abc\",\"uniKey\":\"key-1\"}}");
    netease_official_client_t client = make_client();
    netease_official_qr_t qr = {0};
    assert(netease_official_qr_start(&client, "anon-token", &qr) == ESP_OK);
    assert(strcmp(qr.qr_code_url, "https://163cn.tv/abc") == 0);
    assert(strcmp(qr.unikey, "key-1") == 0);
    assert(qr.expires_in_seconds == 300);
    assert(strstr(g_last_query, "accessToken=anon-token") != NULL);
    assert(strstr(g_last_query, "bizContent=%7B%22type%22%3A2%2C%22expiredKey%22%3A%22300%22%7D") != NULL);
}

static void test_qr_start_accepts_official_sized_access_token(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_QR_START,
                 "{\"code\":200,\"subCode\":null,\"message\":null,\"data\":{\"qrCodeUrl\":\"https://163cn.tv/abc\",\"uniKey\":\"key-1\"}}");
    netease_official_client_t client = make_client();
    char long_token[685] = {0};
    memset(long_token, '/', sizeof(long_token) - 2);
    long_token[sizeof(long_token) - 2] = '=';
    netease_official_qr_t qr = {0};
    assert(netease_official_qr_start(&client, long_token, &qr) == ESP_OK);
    assert(strstr(g_last_query, "accessToken=%2F%2F%2F") != NULL);
}

static void test_qr_poll_maps_confirmed_tokens(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_QR_POLL,
                 "{\"code\":200,\"data\":{\"accessToken\":{\"accessToken\":\"user-token\",\"refreshToken\":\"refresh-token\",\"expireTime\":604800,\"scopes\":null},\"status\":803,\"msg\":\"扫码成功\"},\"message\":\"\"}");
    netease_official_client_t client = make_client();
    netease_official_qr_status_t status = {0};
    assert(netease_official_qr_poll(&client, "anon-token", "key-1", &status) == ESP_OK);
    assert(status.state == NETEASE_OFFICIAL_QR_CONFIRMED);
    assert(strcmp(status.tokens.access_token, "user-token") == 0);
    assert(strcmp(status.tokens.refresh_token, "refresh-token") == 0);
    assert(status.tokens.access_expires_at_ms > 0);
    assert(strstr(g_last_query, "bizContent=%7B%22key%22%3A%22key-1%22%2C%22clientId%22%3A%22app-test%22%7D") != NULL);
}

static void test_refresh_uses_client_secret_and_reads_expires_time(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_TOKEN_REFRESH,
                 "{\"code\":200,\"data\":{\"accessToken\":\"new-token\",\"refreshToken\":\"new-refresh\",\"expiresTime\":604800}}");
    netease_official_client_t client = make_client();
    netease_token_set_t tokens = {0};
    assert(netease_official_refresh(&client, "old-refresh", &tokens) == ESP_OK);
    assert(strcmp(tokens.access_token, "new-token") == 0);
    assert(strcmp(tokens.refresh_token, "new-refresh") == 0);
    assert(strstr(g_last_query, "bizContent=%7B%22clientId%22%3A%22app-test%22%2C%22clientSecret%22%3A%22secret-test%22%2C%22refreshToken%22%3A%22old-refresh%22%7D") != NULL);
}

static void test_playlists_merge_created_and_subscribed_records(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_PLAYLIST_CREATED,
                 "{\"code\":200,\"data\":{\"recordCount\":1,\"records\":[{\"id\":\"created-1\",\"name\":\"自建\",\"creatorNickName\":\"me\",\"trackCount\":12}]}}");
    push_fixture(NETEASE_OFFICIAL_PATH_PLAYLIST_SUBSCRIBED,
                 "{\"code\":200,\"data\":{\"recordCount\":2,\"records\":[{\"id\":\"created-1\",\"name\":\"重复\",\"creatorNickName\":\"me\",\"trackCount\":12},{\"id\":\"sub-1\",\"name\":\"收藏\",\"creatorNickName\":\"netease\",\"trackCount\":34}]}}");
    netease_official_client_t client = make_client();
    netease_official_playlist_list_t out = {0};
    assert(netease_official_get_playlists(&client, "user-token", &out) == ESP_OK);
    assert(out.count == 2);
    assert(strcmp(out.items[0].id, "created-1") == 0);
    assert(strcmp(out.items[1].id, "sub-1") == 0);
}

static void test_tracks_parse_playlist_song_list_v5(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_PLAYLIST_TRACKS,
                 "{\"code\":200,\"data\":{\"id\":\"pl-1\",\"name\":\"歌单\",\"trackCount\":31,\"tracks\":[{\"id\":\"song-1\",\"name\":\"歌名\",\"duration\":251000,\"artists\":[{\"name\":\"歌手\"}]}]}}");
    netease_official_client_t client = make_client();
    netease_official_track_list_t out = {0};
    assert(netease_official_get_tracks(&client, "user-token", "pl-1", 30, 0, &out) == ESP_OK);
    assert(strcmp(out.playlist_id, "pl-1") == 0);
    assert(out.count == 1);
    assert(out.has_more == true);
    assert(out.next_offset == 30);
    assert(strcmp(out.items[0].id, "song-1") == 0);
    assert(strcmp(out.items[0].name, "歌名") == 0);
    assert(strcmp(out.items[0].artist, "歌手") == 0);
    assert(out.items[0].duration_ms == 251000);
    assert(strstr(g_last_query, "bizContent=%7B%22playlistId%22%3A%22pl-1%22%2C%22limit%22%3A30%2C%22offset%22%3A0%2C%22qualityFlag%22%3Atrue%7D") != NULL);
}

static void test_play_url_accepts_mp3_url(void)
{
    reset_fixtures();
    push_fixture(NETEASE_OFFICIAL_PATH_PLAY_URL,
                 "{\"code\":200,\"subCode\":\"200\",\"message\":null,\"data\":{\"url\":\"http://m801.music.126.net/song.mp3\",\"size\":1202721,\"md5\":\"abc\",\"br\":128000,\"privateCloudSong\":false,\"level\":\"standard\",\"freeTrial\":{\"start\":0,\"end\":30}}}");
    netease_official_client_t client = make_client();
    netease_official_play_url_t out = {0};
    assert(netease_official_get_play_url(&client, "user-token", "song-1", 128000, &out) == ESP_OK);
    assert(strcmp(out.song_id, "song-1") == 0);
    assert(strcmp(out.url, "http://m801.music.126.net/song.mp3") == 0);
    assert(strcmp(out.format, "mp3") == 0);
    assert(out.expires_in_seconds == 1500);
    assert(strstr(g_last_query, "bizContent=%7B%22songId%22%3A%22song-1%22%2C%22bitrate%22%3A128000%7D") != NULL);
}
#endif

int main(void)
{
#ifdef NETEASE_OFFICIAL_HOST_TEST
    test_anonymous_login_builds_clientid_and_reads_token();
    test_qr_start_uses_type_and_expiry_and_reads_url_key();
    test_qr_start_accepts_official_sized_access_token();
    test_qr_poll_maps_confirmed_tokens();
    test_refresh_uses_client_secret_and_reads_expires_time();
    test_playlists_merge_created_and_subscribed_records();
    test_tracks_parse_playlist_song_list_v5();
    test_play_url_accepts_mp3_url();
#endif
    return 0;
}
