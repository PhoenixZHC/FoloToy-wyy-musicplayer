#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provisioning_portal.h"

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--html") == 0) {
        fputs(wifi_provisioning_portal_html(), stdout);
        return 0;
    }

    const wifi_provisioning_scan_entry_t scanned[] = {
        {.ssid = "Weak", .rssi = -82, .secure = true},
        {.ssid = "Home WiFi", .rssi = -42, .secure = true},
        {.ssid = "", .rssi = -10, .secure = false},
        {.ssid = "Home WiFi", .rssi = -68, .secure = true},
        {.ssid = "Cafe\"Guest", .rssi = -55, .secure = false},
    };
    wifi_provisioning_scan_entry_t visible[3] = {0};

    size_t count = wifi_provisioning_prepare_scan_results(
        scanned,
        sizeof(scanned) / sizeof(scanned[0]),
        visible,
        sizeof(visible) / sizeof(visible[0]));

    assert(count == 3);
    assert(strcmp(visible[0].ssid, "Home WiFi") == 0);
    assert(visible[0].rssi == -42);
    assert(strcmp(visible[1].ssid, "Cafe\"Guest") == 0);
    assert(strcmp(visible[2].ssid, "Weak") == 0);

    char json[256] = {0};
    size_t json_len = wifi_provisioning_scan_results_json(visible, count, json, sizeof(json));
    assert(json_len > 0);
    assert(strcmp(json,
                  "{\"networks\":[{\"ssid\":\"Home WiFi\",\"rssi\":-42,\"secure\":true},"
                  "{\"ssid\":\"Cafe\\\"Guest\",\"rssi\":-55,\"secure\":false},"
                  "{\"ssid\":\"Weak\",\"rssi\":-82,\"secure\":true}]}") == 0);
    assert(wifi_provisioning_scan_results_json(visible, count, json, 16) == 0);

    const char *html = wifi_provisioning_portal_html();
    assert(html != NULL);
    assert(strstr(html, "fetch('/scan')") != NULL);
    assert(strstr(html, "name='ssid'") != NULL);
    assert(strstr(html, "name='password'") != NULL);
    assert(strstr(html, "手动输入网络名称") != NULL);
    assert(strstr(html, "重新扫描") != NULL);
    assert(strstr(html, "连接设备") != NULL);

    const char *success_html = wifi_provisioning_success_html();
    assert(success_html != NULL);
    assert(strstr(success_html, "正在连接") != NULL);
    assert(strstr(success_html, "请回到设备屏幕") != NULL);
    return 0;
}
