#pragma once
#include <string.h>
#include "esp_err.h"
#define ESP_MAC_WIFI_STA 0
static inline esp_err_t esp_read_mac(unsigned char *mac, int type)
{
    (void)type;
    const unsigned char address[6] = {2, 0, 0, 0, 0, 1};
    memcpy(mac, address, sizeof(address));
    return ESP_OK;
}
