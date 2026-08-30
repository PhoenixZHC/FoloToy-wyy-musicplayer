#include <assert.h>
#include <stddef.h>

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

    assert(wifi_provisioning_can_submit("HomeWiFi"));
    assert(!wifi_provisioning_can_submit(""));
    assert(!wifi_provisioning_can_submit(NULL));
    assert(wifi_provisioning_has_ready_config("HomeWiFi"));
    assert(!wifi_provisioning_has_ready_config(""));
    assert(!wifi_provisioning_has_ready_config(NULL));
    return 0;
}
