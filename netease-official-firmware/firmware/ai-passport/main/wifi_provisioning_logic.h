#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_PROVISIONING_NEEDS_SETUP = 0,
    WIFI_PROVISIONING_CONNECTING,
    WIFI_PROVISIONING_CONNECTED,
    WIFI_PROVISIONING_FAILED,
} wifi_provisioning_state_t;

typedef struct {
    wifi_provisioning_state_t state;
    bool has_saved_config;
} wifi_provisioning_logic_t;

void wifi_provisioning_logic_init(wifi_provisioning_logic_t *logic, bool has_saved_config);
wifi_provisioning_state_t wifi_provisioning_logic_state(const wifi_provisioning_logic_t *logic);
void wifi_provisioning_logic_on_connect_result(wifi_provisioning_logic_t *logic, bool connected);
void wifi_provisioning_logic_on_ok_click(wifi_provisioning_logic_t *logic);
void wifi_provisioning_logic_on_ok_long(wifi_provisioning_logic_t *logic);
bool wifi_provisioning_can_submit(const char *ssid);
bool wifi_provisioning_has_ready_config(const char *ssid);
