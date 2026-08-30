// components/bsp/src/bsp_audio.c
// 移植自 trae_card/components/platform/platform_esp32/src/audio_es8311.c
#include "bsp_audio.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "bsp_audio";

static esp_codec_dev_handle_t s_dev;
static i2s_chan_handle_t      s_tx;
static const audio_codec_ctrl_if_t *s_ctrl;
static const audio_codec_data_if_t *s_data;
static const audio_codec_if_t *s_codec;
static const audio_codec_gpio_if_t *s_gpio;
// 记录当前已打开的格式,用于判断"要不要 close 重开"(见头文件里的坑说明)。
static uint32_t s_hz;
static uint8_t  s_bits, s_ch;
static bool     s_opened;

static esp_err_t i2s_tx_init(void) {
    i2s_chan_config_t chan = {
        .id = BSP_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 512,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t e = i2s_new_channel(&chan, &s_tx, NULL);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel 失败: %s", esp_err_to_name(e)); return e; }

    // 这里的采样率只用于建通道;实际速率由 esp_codec_dev_open() 按需重配。
    i2s_std_config_t std = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_BCLK, .ws = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT, .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    if ((e = i2s_channel_init_std_mode(s_tx, &std)) != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx 初始化失败: %s", esp_err_to_name(e)); return e;
    }
    // esp_codec_dev_open 内部重配前会先 i2s_channel_disable,而 disable 要求通道处于
    // RUNNING;刚 init 的通道是 READY,会打一条 "channel has not been enabled yet" 错误日志。
    // 这里先 enable 一次让那次 disable 合法(此时 codec 未配,不出声)。
    i2s_channel_enable(s_tx);
    return ESP_OK;
}

esp_err_t bsp_audio_init(void) {
    if (s_dev) return ESP_OK;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    s_ctrl = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port = BSP_I2C_PORT,
        .addr = BSP_I2C_ES8311_ADDR << 1,   // 该接口要 8 位地址形式
        .bus_handle = bsp_i2c_bus(),
    });
    if (!s_ctrl) {
        ESP_LOGE(TAG, "ES8311 控制口创建失败 —— 用 bsp_i2c_scan() 确认 0x%02X 是否应答;"
                      "检查 SDA=GPIO%d / SCL=GPIO%d 接线与 codec 供电",
                 BSP_I2C_ES8311_ADDR, BSP_I2C_SDA, BSP_I2C_SCL);
        return ESP_FAIL;
    }

    if ((e = i2s_tx_init()) != ESP_OK) {
        bsp_audio_deinit();
        return e;
    }

    s_data = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){
        .port = BSP_I2S_PORT, .tx_handle = s_tx, .rx_handle = NULL,
    });
    if (!s_data) {
        ESP_LOGE(TAG, "I2S 数据口创建失败");
        bsp_audio_deinit();
        return ESP_FAIL;
    }

    s_gpio = audio_codec_new_gpio();
    if (!s_gpio) {
        ESP_LOGE(TAG, "音频 GPIO 接口创建失败");
        bsp_audio_deinit();
        return ESP_FAIL;
    }
    s_codec = es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if     = s_ctrl,
        .gpio_if     = s_gpio,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = BSP_I2S_PA_CTRL,
        .pa_reverted = false,
        .master_mode = false,          // MCU I2S 为 master,codec 为 slave
        .use_mclk    = true,
        .hw_gain     = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
        // ⚠ 单声道纯麦克风录音必须为 true。false 会让驱动写 REG44=0x58 进入
        //   ADCL+DACR 参考模式,单声道读到的那一路是 DAC 参考 → 【录音恒为 0】。
        .no_dac_ref  = true,
    });
    if (!s_codec) {
        ESP_LOGE(TAG, "es8311_codec_new 失败");
        bsp_audio_deinit();
        return ESP_FAIL;
    }

    s_dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_codec,
        .data_if  = s_data,
    });
    if (!s_dev) {
        ESP_LOGE(TAG, "esp_codec_dev_new 失败");
        bsp_audio_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ES8311 就绪");
    return ESP_OK;
}

esp_err_t bsp_audio_deinit(void) {
    bool was_opened = s_opened;
    if (was_opened && s_dev) esp_codec_dev_close(s_dev);
    s_opened = false;
    s_hz = 0;
    s_bits = 0;
    s_ch = 0;
    if (s_dev) {
        esp_codec_dev_delete(s_dev);
        s_dev = NULL;
    }
    if (s_codec) {
        audio_codec_delete_codec_if(s_codec);
        s_codec = NULL;
    }
    if (s_ctrl) {
        audio_codec_delete_ctrl_if(s_ctrl);
        s_ctrl = NULL;
    }
    if (s_data) {
        audio_codec_delete_data_if(s_data);
        s_data = NULL;
    }
    if (s_gpio) {
        audio_codec_delete_gpio_if(s_gpio);
        s_gpio = NULL;
    }
    if (s_tx) {
        if (!was_opened) i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    return ESP_OK;
}

esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (s_opened && s_hz == hz && s_bits == bits && s_ch == ch) return ESP_OK;   // 同格式复用

    if (s_opened) {
        esp_codec_dev_close(s_dev);
        s_opened = false;
        // close 把 I2S 通道退回 READY,而接下来的 open 内部又会 disable 一次 →
        // 会打 "channel has not been enabled yet"。补一次 enable 让它合法。
        if (s_tx) i2s_channel_enable(s_tx);
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits,
        .channel = ch,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = hz,
        .mclk_multiple = 0,          // 0 → 驱动按默认 256xfs 取 MCLK
    };
    int r = esp_codec_dev_open(s_dev, &fs);
    if (r != 0) { ESP_LOGE(TAG, "esp_codec_dev_open 失败: %d", r); return ESP_FAIL; }

    s_opened = true; s_hz = hz; s_bits = bits; s_ch = ch;
    ESP_LOGI(TAG, "codec 打开 %luHz/%ubit/%uch", (unsigned long)hz, bits, ch);
    return ESP_OK;
}

esp_err_t bsp_audio_write(const void *pcm, size_t bytes) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_write(s_dev, (void *)pcm, bytes) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_read(void *pcm, size_t bytes) {
    (void)pcm;
    (void)bytes;
    return ESP_ERR_NOT_SUPPORTED;
}

void bsp_audio_set_volume(uint8_t percent) {
    if (s_dev) esp_codec_dev_set_out_vol(s_dev, percent);
}
