#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_PROVISIONING_NEEDS_SETUP = 0,
    WIFI_PROVISIONING_CONNECTING,
    WIFI_PROVISIONING_CONNECTED,
    WIFI_PROVISIONING_FAILED,
} wifi_provisioning_state_t;

typedef enum {
    WIFI_PROVISIONING_STA_CONNECT = 0,
    WIFI_PROVISIONING_STA_REUSE,
    WIFI_PROVISIONING_STA_RECONNECT,
} wifi_provisioning_station_action_t;

typedef struct {
    wifi_provisioning_state_t state;
    bool has_saved_config;
} wifi_provisioning_logic_t;

void wifi_provisioning_logic_init(wifi_provisioning_logic_t *logic, bool has_saved_config);
wifi_provisioning_state_t wifi_provisioning_logic_state(const wifi_provisioning_logic_t *logic);
void wifi_provisioning_logic_on_connect_result(wifi_provisioning_logic_t *logic, bool connected);
void wifi_provisioning_logic_on_ok_click(wifi_provisioning_logic_t *logic);
void wifi_provisioning_logic_on_ok_long(wifi_provisioning_logic_t *logic);
bool wifi_provisioning_is_valid_cloud_url(const char *url);
bool wifi_provisioning_can_submit(const char *ssid, const char *cloud_url, const char *registration_code);
bool wifi_provisioning_has_ready_config(const char *cloud_url,
                                        const char *device_id,
                                        const char *device_secret,
                                        bool registered);
bool wifi_provisioning_should_run_mock_smoke(const char *ssid);
wifi_provisioning_station_action_t wifi_provisioning_station_action(bool station_connected,
                                                                    bool has_ip,
                                                                    const char *current_ssid,
                                                                    const char *requested_ssid);
bool wifi_provisioning_password_is_valid(const char *password);

#define WIFI_PASSWORD_CAPACITY 64
typedef struct {
    char text[WIFI_PASSWORD_CAPACITY + 1];
    unsigned group;
    unsigned character;
    bool visible;
} wifi_password_editor_t;
void wifi_password_editor_init(wifi_password_editor_t *editor);
void wifi_password_editor_move(wifi_password_editor_t *editor, int direction, bool group);
char wifi_password_editor_candidate(const wifi_password_editor_t *editor);
const char *wifi_password_editor_group(const wifi_password_editor_t *editor);
const char *wifi_password_editor_characters(const wifi_password_editor_t *editor);
bool wifi_password_editor_add(wifi_password_editor_t *editor);
void wifi_password_editor_delete(wifi_password_editor_t *editor);
