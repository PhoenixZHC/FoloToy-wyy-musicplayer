#include <assert.h>

#include "netease_official_client.h"

static const char *VALID_TEST_KEY =
    "-----BEGIN PRIVATE KEY-----\n"
    "test-only-key-body\n"
    "-----END PRIVATE KEY-----\n";

static netease_official_config_t valid_config(void)
{
    netease_official_config_t config = {
        .app_id = "app-test",
        .app_secret = "secret-test",
        .private_key_pem = VALID_TEST_KEY,
        .device_id = "esp32c3-test",
        .app_version = "1.0.0",
        .timeout_ms = 10000,
    };
    return config;
}

int main(void)
{
    netease_official_client_t client = {0};
    netease_official_config_t config = valid_config();
    assert(netease_official_init(&client, &config) == ESP_OK);

    config = valid_config();
    config.app_id = "replace-me";
    assert(netease_official_init(&client, &config) == ESP_ERR_INVALID_STATE);

    config = valid_config();
    config.app_secret = "";
    assert(netease_official_init(&client, &config) == ESP_ERR_INVALID_STATE);

    config = valid_config();
    config.private_key_pem = "test-only-key-body";
    assert(netease_official_init(&client, &config) == ESP_ERR_INVALID_STATE);

    config = valid_config();
    config.device_id = "";
    assert(netease_official_init(&client, &config) == ESP_ERR_INVALID_STATE);

    return 0;
}
