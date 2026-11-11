// components/bsp/src/bsp_battery.c
// 移植自 trae_card/components/platform/platform_esp32/src/battery_cw2017.c
// 启动时写入默认 4.2 V Li-Poly profile；未进行电芯完整充放电标定。
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_batt";

#define CW_REG_VERSION   0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H   0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H     0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_CONFIG    0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常
#define CW_REG_MODE      0x0A
#define CW_REG_BATINFO   0x10

/* 默认 4.2 V Li-Poly 参数，不是芯片 ROM profile，也不代表本电芯已经标定。 */
static const uint8_t s_default_profile[80] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xB7,0xC2,0xB6,0xA9,0x9E,0x99,0xED,0xDF,
    0xD9,0xCB,0xC1,0xAD,0x9D,0x80,0x66,0x55,
    0x4B,0x4B,0x4B,0x85,0x7F,0xD3,0x73,0xFF,
    0xF9,0x5C,0x63,0x81,0xBD,0xEC,0xE0,0xD0,
    0xC7,0xD7,0xD6,0xD9,0xE5,0xDF,0xDA,0xD6,
    0xCD,0xCF,0xD3,0xD3,0xD5,0xF4,0xFF,0x43,
    0x00,0x00,0xAB,0x02,0x00,0x00,0x00,0x00,
    0x00,0x00,0x64,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,
};

static i2c_master_dev_handle_t s_dev;

static int cw_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (!s_dev) return -1;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK ? 0 : -1;
}

static int cw_write(uint8_t reg, uint8_t val) {
    if (!s_dev) return -1;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK ? 0 : -1;
}

static int cw_write_profile(void) {
    for (size_t i = 0; i < sizeof(s_default_profile); ++i) {
        if (cw_write((uint8_t)(CW_REG_BATINFO + i), s_default_profile[i]) != 0) return -1;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

esp_err_t bsp_battery_init(void) {
    if (s_dev) return ESP_OK;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_I2C_CW2017_ADDR,
        .scl_speed_hz    = 100000,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dc, &s_dev);
    if (e != ESP_OK) { ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(e)); return e; }

    uint8_t ver = 0;
    if (cw_read(CW_REG_VERSION, &ver, 1) != 0) {
        ESP_LOGW(TAG, "CW2017 未应答 —— 用 bsp_i2c_scan() 确认 0x%02X 是否在线;"
                      "无电量计的板子可忽略本项", BSP_I2C_CW2017_ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);
    uint8_t diagnostic[16] = {0};
    if (cw_read(0x00, diagnostic, sizeof(diagnostic)) == 0) {
        ESP_LOGI(TAG, "Gauge registers before init: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                 diagnostic[0], diagnostic[1], diagnostic[2], diagnostic[3],
                 diagnostic[4], diagnostic[5], diagnostic[6], diagnostic[7],
                 diagnostic[8], diagnostic[9], diagnostic[10], diagnostic[11],
                 diagnostic[12], diagnostic[13], diagnostic[14], diagnostic[15]);
    }

    /* A fresh/volatile gauge reports 0xFE until its profile is applied. */
    if (cw_write(CW_REG_CONFIG, 0xF0) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (cw_write_profile() != 0) return ESP_FAIL;
    if (cw_write(CW_REG_CONFIG, 0x02) != 0) return ESP_FAIL;
    if (cw_write(CW_REG_MODE, 0x00) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (cw_write(CW_REG_MODE, 0x40) != 0) return ESP_FAIL;
    if (cw_write(CW_REG_CONFIG, 0x30) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (cw_write(CW_REG_CONFIG, 0x00) != 0) return ESP_FAIL;
    int soc = -1, mv = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
        soc = bsp_battery_soc();
        mv = bsp_battery_mv();
        if (soc > 0 && mv > 0) break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_LOGI(TAG, "Gauge initial reading: soc=%d voltage_mv=%d", soc, mv);

    return ESP_OK;
}

int bsp_battery_soc(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_SOC_H, b, 2) != 0) return -1;
    int soc = b[0];                       // 高字节即整数百分比
    if (soc > 100) return -1;             // 芯片未就绪时可能读到 0xFF
    return soc;
}

int bsp_battery_mv(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_VCELL_H, b, 2) != 0) return -1;
    uint32_t raw = ((uint32_t)b[0] << 8 | b[1]) & 0x3FFF;   // 14bit
    return (int)((raw * 3125) / 10000);                     // raw * 312.5uV → mV
}
