#include <assert.h>

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
    return 0;
}
