#pragma once

#include "bsp_button.h"
#include "display_policy.h"
#include "esp_err.h"

/* 初始化、设置和按键处理均需持有 LVGL 锁；刷新回调也在该锁内运行。 */
esp_err_t display_settings_init(void (*refresh)(void));
bool display_settings_on_key(bsp_btn_ev_t event);
int display_settings_brightness(void);
void display_settings_set_brightness(int percent);
uint16_t display_settings_timeout(void);
void display_settings_set_timeout(uint16_t seconds);
const char *display_settings_clock_status(void);
/* 仅供工作任务调用：短暂加锁取快照，解锁后写入 NVS。 */
esp_err_t display_settings_save(void);
