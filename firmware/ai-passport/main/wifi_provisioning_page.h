#pragma once

#include "bsp_button.h"

#include <stdbool.h>

void wifi_provisioning_page_request_setup(void);
bool wifi_provisioning_page_setup_requested(void);
void wifi_provisioning_page_enter(void);
void wifi_provisioning_page_exit(void);
void wifi_provisioning_page_key(bsp_btn_t btn, bsp_btn_ev_t ev);
bool wifi_provisioning_page_has_saved_config(void);
void wifi_provisioning_page_set_complete_callback(void (*callback)(void));
