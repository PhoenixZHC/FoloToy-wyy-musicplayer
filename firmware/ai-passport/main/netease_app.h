#pragma once

#include "bsp_button.h"

void netease_app_enter(void);
void netease_app_exit(void);
void netease_app_set_reset_callback(void (*callback)(void));
void netease_app_key(bsp_btn_t btn, bsp_btn_ev_t ev);
/* 调用方持有 LVGL 锁，用于日夜变化和唤醒后的页面刷新。 */
void netease_app_refresh_display(void);
