#include "wifi_provisioning_logic.h"

#include <stddef.h>

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

static bool has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

bool wifi_provisioning_can_submit(const char *ssid)
{
    return has_text(ssid);
}

bool wifi_provisioning_has_ready_config(const char *ssid)
{
    return has_text(ssid);
}
