#pragma once

#include "bsp_button.h"

void netease_app_enter(void);
void netease_app_exit(void);
void netease_app_set_reset_callback(void (*callback)(void));
void netease_app_key(bsp_btn_t btn, bsp_btn_ev_t ev);
