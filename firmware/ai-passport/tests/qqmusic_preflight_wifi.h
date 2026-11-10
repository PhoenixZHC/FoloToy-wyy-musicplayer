#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "esp_wifi.h"

/* 仅预检串口初始化窗口接收 JSON；保存到独立测试命名空间。 */
esp_err_t qqmusic_preflight_wifi_saved(wifi_config_t *config);

/* 仅供预检：现有 Wi-Fi 驱动已启动且使用 RAM 存储，不写 NVS。 */
esp_err_t qqmusic_preflight_wifi_setup(volatile bool *connected,
                                     void (*display)(const char *,const char *));
