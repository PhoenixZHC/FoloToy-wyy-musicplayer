#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_PROVISIONING_SSID_MAX_BYTES 32
#define WIFI_PROVISIONING_SCAN_MAX_RESULTS 12

typedef struct {
    char ssid[WIFI_PROVISIONING_SSID_MAX_BYTES + 1];
    int8_t rssi;
    bool secure;
} wifi_provisioning_scan_entry_t;

size_t wifi_provisioning_prepare_scan_results(
    const wifi_provisioning_scan_entry_t *input,
    size_t input_count,
    wifi_provisioning_scan_entry_t *output,
    size_t max_output);

size_t wifi_provisioning_scan_results_json(
    const wifi_provisioning_scan_entry_t *entries,
    size_t entry_count,
    char *output,
    size_t output_size);

const char *wifi_provisioning_portal_html(void);
const char *wifi_provisioning_success_html(void);
