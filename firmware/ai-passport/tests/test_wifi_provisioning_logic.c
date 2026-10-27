#include <assert.h>
#include <string.h>

#include "wifi_provisioning_logic.h"

int main(void)
{
    wifi_provisioning_logic_t logic = {0};

    wifi_provisioning_logic_init(&logic, false);
    assert(wifi_provisioning_logic_state(&logic) == WIFI_PROVISIONING_NEEDS_SETUP);

    wifi_provisioning_logic_init(&logic, true);
    assert(wifi_provisioning_logic_state(&logic) == WIFI_PROVISIONING_CONNECTING);

    wifi_provisioning_logic_on_connect_result(&logic, false);
    assert(wifi_provisioning_logic_state(&logic) == WIFI_PROVISIONING_FAILED);

    wifi_provisioning_logic_on_ok_click(&logic);
    assert(wifi_provisioning_logic_state(&logic) == WIFI_PROVISIONING_CONNECTING);

    wifi_provisioning_logic_on_ok_long(&logic);
    assert(wifi_provisioning_logic_state(&logic) == WIFI_PROVISIONING_NEEDS_SETUP);

    assert(wifi_provisioning_is_valid_cloud_url("http://192.168.1.2:8080"));
    assert(wifi_provisioning_is_valid_cloud_url("https://music.example.com"));
    assert(!wifi_provisioning_is_valid_cloud_url(""));
    assert(!wifi_provisioning_is_valid_cloud_url("ftp://192.168.1.2"));
    assert(wifi_provisioning_can_submit("", "http://192.168.4.2:18080", "pair-123"));
    assert(!wifi_provisioning_can_submit("HomeWiFi", "http://192.168.4.2:18080", "pair-123"));
    assert(wifi_provisioning_can_submit("HomeWiFi", "http://192.168.5.18:18080", "pair-123"));
    assert(!wifi_provisioning_can_submit("", "http://192.168.4.2:18080", ""));
    assert(!wifi_provisioning_can_submit("", "ftp://192.168.4.2:18080", "pair-123"));
    assert(!wifi_provisioning_has_ready_config("http://192.168.4.2:18080", "dev_a", "secret_a_32_bytes_minimum_value_x", false));
    assert(wifi_provisioning_has_ready_config("http://192.168.4.2:18080", "dev_a", "secret_a_32_bytes_minimum_value_x", true));
    assert(wifi_provisioning_should_run_mock_smoke(""));
    assert(!wifi_provisioning_should_run_mock_smoke("HomeWiFi"));

    assert(wifi_provisioning_station_action(false, false, "", "HomeWiFi") ==
           WIFI_PROVISIONING_STA_CONNECT);
    assert(wifi_provisioning_station_action(true, true, "HomeWiFi", "HomeWiFi") ==
           WIFI_PROVISIONING_STA_REUSE);
    assert(wifi_provisioning_station_action(true, false, "HomeWiFi", "HomeWiFi") ==
           WIFI_PROVISIONING_STA_RECONNECT);
    assert(wifi_provisioning_station_action(true, true, "OldWiFi", "HomeWiFi") ==
           WIFI_PROVISIONING_STA_RECONNECT);

    assert(!wifi_provisioning_password_is_valid(NULL));
    assert(wifi_provisioning_password_is_valid(""));
    assert(!wifi_provisioning_password_is_valid("short"));
    assert(wifi_provisioning_password_is_valid(" Ab9! xyz"));
    assert(!wifi_provisioning_password_is_valid("abcdefg\n"));
    char full[66]; memset(full, 'a', 64); full[64] = 0;
    assert(wifi_provisioning_password_is_valid(full));
    full[63] = 'z'; assert(!wifi_provisioning_password_is_valid(full));
    full[64] = 'a'; full[65] = 0; assert(!wifi_provisioning_password_is_valid(full));
    wifi_password_editor_t editor;
    wifi_password_editor_init(&editor);
    assert(!editor.visible && !editor.text[0]);
    assert(wifi_password_editor_candidate(&editor) == 'q');
    assert(!strcmp(wifi_password_editor_characters(&editor), "qwertyuiopasdfghjklzxcvbnm"));
    assert(wifi_password_editor_candidate(&editor) == 'q');
    wifi_password_editor_move(&editor, -1, false);
    assert(wifi_password_editor_candidate(&editor) == 'm');
    wifi_password_editor_move(&editor, 1, false);
    assert(wifi_password_editor_candidate(&editor) == 'q');
    bool reachable[127] = {false};
    for (unsigned group = 0; group < 4; ++group) {
        char first = wifi_password_editor_candidate(&editor);
        const char *characters = wifi_password_editor_characters(&editor);
        size_t group_length = strlen(characters);
        assert(group_length == (group < 2 ? 26 : group == 2 ? 10 : 33));
        do {
            char c = wifi_password_editor_candidate(&editor);
            assert(c >= 32 && c <= 126); reachable[(unsigned)c] = true;
            wifi_password_editor_move(&editor, 1, false);
        } while (wifi_password_editor_candidate(&editor) != first);
        for (size_t i = 0; i < group_length; ++i)
            for (size_t j = i + 1; j < group_length; ++j) assert(characters[i] != characters[j]);
        wifi_password_editor_move(&editor, 1, true);
    }
    for (unsigned c = 32; c <= 126; ++c) assert(reachable[c]);
    wifi_password_editor_move(&editor, -1, true);
    wifi_password_editor_move(&editor, -1, false);
    assert(wifi_password_editor_candidate(&editor) == ' ');
    assert(wifi_password_editor_add(&editor)); assert(!strcmp(editor.text, " "));
    wifi_password_editor_delete(&editor); wifi_password_editor_delete(&editor);
    for (unsigned i = 0; i < 64; ++i) assert(wifi_password_editor_add(&editor));
    assert(!wifi_password_editor_add(&editor) && strlen(editor.text) == 64);
    wifi_password_editor_delete(&editor); assert(strlen(editor.text) == 63);
    wifi_password_editor_init(&editor); assert(!editor.text[0]);
    return 0;
}
