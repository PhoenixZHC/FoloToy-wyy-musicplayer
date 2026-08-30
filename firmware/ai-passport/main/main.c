// main/main.c —— 独立网易云设备应用入口。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "netease_app.h"
#include "wifi_provisioning_page.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "main";

typedef enum {
    APP_PAGE_CLOUD_SETUP = 0,
    APP_PAGE_NETEASE,
} app_page_t;

static app_page_t s_page;
static bool s_page_active;

static void enter_netease_app(void);

static void close_active_page(void)
{
    if (!s_page_active) return;
    if (s_page == APP_PAGE_CLOUD_SETUP) {
        wifi_provisioning_page_exit();
    } else {
        netease_app_exit();
    }
    s_page_active = false;
}

static void enter_cloud_setup(void)
{
    close_active_page();
    s_page = APP_PAGE_CLOUD_SETUP;
    s_page_active = true;
    wifi_provisioning_page_set_complete_callback(enter_netease_app);
    wifi_provisioning_page_enter();
}

static void enter_netease_app(void)
{
    close_active_page();
    s_page = APP_PAGE_NETEASE;
    s_page_active = true;
    netease_app_set_reset_callback(enter_cloud_setup);
    netease_app_enter();
}

// 按键回调运行在 button 组件的任务里,操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    if (s_page == APP_PAGE_CLOUD_SETUP) {
        wifi_provisioning_page_key(btn, ev);
    } else {
        netease_app_key(btn, ev);
    }
    bsp_lvgl_unlock();
}

void app_main(void) {
    ESP_LOGI(TAG, "网易云设备应用启动");
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "休眠唤醒原因: %d", wakeup);
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是设备应用的 UI 载体,失败后直接退出并打清楚日志。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,demo 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    esp_err_t button_err = bsp_button_init(on_key, NULL);
    if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败:%s", esp_err_to_name(button_err));
    }
    esp_err_t battery_err = bsp_battery_init();
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "电池检测初始化失败:%s", esp_err_to_name(battery_err));
    }

    bool has_cloud_config = wifi_provisioning_page_has_saved_config();
    if (bsp_lvgl_lock(1000)) {
        if (!has_cloud_config) {
            enter_cloud_setup();
        } else {
            enter_netease_app();
        }
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "独立网易云设备应用已启动");
}
