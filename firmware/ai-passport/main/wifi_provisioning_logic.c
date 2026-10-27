#include "wifi_provisioning_logic.h"

#include <string.h>
#include <ctype.h>

void wifi_provisioning_logic_init(wifi_provisioning_logic_t *logic, bool has_saved_config)
{
    if (logic == NULL) {
        return;
    }
    logic->has_saved_config = has_saved_config;
    logic->state = has_saved_config ? WIFI_PROVISIONING_CONNECTING : WIFI_PROVISIONING_NEEDS_SETUP;
}

wifi_provisioning_state_t wifi_provisioning_logic_state(const wifi_provisioning_logic_t *logic)
{
    if (logic == NULL) {
        return WIFI_PROVISIONING_FAILED;
    }
    return logic->state;
}

void wifi_provisioning_logic_on_connect_result(wifi_provisioning_logic_t *logic, bool connected)
{
    if (logic == NULL) {
        return;
    }
    logic->state = connected ? WIFI_PROVISIONING_CONNECTED : WIFI_PROVISIONING_FAILED;
}

void wifi_provisioning_logic_on_ok_click(wifi_provisioning_logic_t *logic)
{
    if (logic == NULL) {
        return;
    }
    if (logic->state == WIFI_PROVISIONING_FAILED && logic->has_saved_config) {
        logic->state = WIFI_PROVISIONING_CONNECTING;
    }
}

void wifi_provisioning_logic_on_ok_long(wifi_provisioning_logic_t *logic)
{
    if (logic == NULL) {
        return;
    }
    logic->has_saved_config = false;
    logic->state = WIFI_PROVISIONING_NEEDS_SETUP;
}

bool wifi_provisioning_is_valid_cloud_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return false;
    }
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

static bool has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

wifi_provisioning_station_action_t wifi_provisioning_station_action(bool station_connected,
                                                                    bool has_ip,
                                                                    const char *current_ssid,
                                                                    const char *requested_ssid)
{
    if (!station_connected) {
        return WIFI_PROVISIONING_STA_CONNECT;
    }
    if (has_ip && has_text(current_ssid) && has_text(requested_ssid) &&
        strcmp(current_ssid, requested_ssid) == 0) {
        return WIFI_PROVISIONING_STA_REUSE;
    }
    return WIFI_PROVISIONING_STA_RECONNECT;
}

bool wifi_provisioning_password_is_valid(const char *password)
{
    if (password == NULL) return false;
    size_t length = strlen(password);
    if (length == 0) return true;
    if (length == 64) {
        for (size_t i = 0; i < length; ++i) {
            if (!isxdigit((unsigned char)password[i])) return false;
        }
        return true;
    }
    if (length < 8 || length > 63) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)password[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

static bool is_softap_peer_url(const char *url)
{
    return strncmp(url, "http://192.168.4.", 17) == 0;
}

bool wifi_provisioning_can_submit(const char *ssid, const char *cloud_url, const char *registration_code)
{
    if (has_text(ssid) && is_softap_peer_url(cloud_url)) {
        return false;
    }
    return wifi_provisioning_is_valid_cloud_url(cloud_url) &&
           registration_code != NULL &&
           registration_code[0] != '\0';
}

bool wifi_provisioning_has_ready_config(const char *cloud_url,
                                        const char *device_id,
                                        const char *device_secret,
                                        bool registered)
{
    return registered &&
           wifi_provisioning_is_valid_cloud_url(cloud_url) &&
           device_id != NULL &&
           device_id[0] != '\0' &&
           device_secret != NULL &&
           device_secret[0] != '\0';
}

bool wifi_provisioning_should_run_mock_smoke(const char *ssid)
{
    return ssid == NULL || ssid[0] == '\0';
}

static const char *const password_groups[] = {
    "qwertyuiopasdfghjklzxcvbnm", "QWERTYUIOPASDFGHJKLZXCVBNM", "1234567890",
    "!@#$%^&*()-_=+[]{};:\",.<>?/\\|`~' "
};
void wifi_password_editor_init(wifi_password_editor_t *editor)
{
    if (editor) memset(editor, 0, sizeof(*editor));
}
void wifi_password_editor_move(wifi_password_editor_t *editor, int direction, bool group)
{
    if (!editor) return;
    if (group) {
        editor->group = (editor->group + (direction < 0 ? 3 : 1)) % 4;
        editor->character = 0;
    } else {
        unsigned length = strlen(password_groups[editor->group % 4]);
        editor->character = (editor->character + (direction < 0 ? length - 1 : 1)) % length;
    }
}
char wifi_password_editor_candidate(const wifi_password_editor_t *editor)
{
    if (!editor) return 0;
    const char *group = password_groups[editor->group % 4];
    return group[editor->character % strlen(group)];
}
const char *wifi_password_editor_group(const wifi_password_editor_t *editor)
{
    static const char *const names[] = {"小写字母", "大写字母", "数字", "符号与空格"};
    return names[editor ? editor->group % 4 : 0];
}
const char *wifi_password_editor_characters(const wifi_password_editor_t *editor)
{
    return editor ? password_groups[editor->group % 4] : password_groups[0];
}
bool wifi_password_editor_add(wifi_password_editor_t *editor)
{
    if (!editor) return false;
    size_t length = strlen(editor->text);
    if (length == WIFI_PASSWORD_CAPACITY) return false;
    editor->text[length] = wifi_password_editor_candidate(editor);
    editor->text[length + 1] = '\0';
    return true;
}
void wifi_password_editor_delete(wifi_password_editor_t *editor)
{
    if (!editor) return;
    size_t length = strlen(editor->text);
    if (length) editor->text[length - 1] = '\0';
}
