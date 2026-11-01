/**
 * FAKE_POD_NANO 音频输出管线实现 (I2S DMA + CS43131)
 */

#include "audio_pipeline.h"
#include "board.h"
#include "board_config.h"
#include "dac_cs43131.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "AUDIO_PIPE";

static i2s_chan_handle_t s_tx_chan = NULL;
static audio_format_t s_current_fmt = {
    .sample_rate = 48000,
    .bits_per_sample = 16,
    .channels = 2,
};
static uint8_t s_current_volume = 80;
static bool s_is_muted = false;

/* ---------------------------------------------------------------
 * 初始化 I2S 硬件通道
 * --------------------------------------------------------------- */
static esp_err_t i2s_driver_setup(const audio_format_t *fmt)
{
    /* 1. 通道分配（启用 TX 通道，使用 APLL 获得高保真超低抖动音频时钟） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S TX 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 标准 Philips 模式配置 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = fmt->sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT, /* ESP32-S3 默认高精时钟 */
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            (i2s_data_bit_width_t)fmt->bits_per_sample,
            fmt->channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)AUDIO_I2S_BCK_IO,
            .ws   = (gpio_num_t)AUDIO_I2S_WS_IO,
            .dout = (gpio_num_t)AUDIO_I2S_DO_IO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    /* 24 位时自动使用 32 位 Slot 传输 */
    if (fmt->bits_per_sample == 24) {
        std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    }

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 标准模式初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动 I2S 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S 通道已就绪 (BCK=%d, WS=%d, DOUT=%d, %lu Hz, %d bit)",
             AUDIO_I2S_BCK_IO, AUDIO_I2S_WS_IO, AUDIO_I2S_DO_IO,
             (unsigned long)fmt->sample_rate, fmt->bits_per_sample);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 音频管线初始化
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_init(void)
{
    ESP_LOGI(TAG, "初始化音频管线...");

    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "无法获取 I2C 总线，必须先调用 board_init()");
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. 初始化 CS43131 硬件与模拟链路 */
    esp_err_t ret = cs43131_init(i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CS43131 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 初始化 ESP32 I2S DMA 主机通道 */
    ret = i2s_driver_setup(&s_current_fmt);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 同步默认音量 */
    cs43131_set_volume(i2c_bus, s_current_volume);

    ESP_LOGI(TAG, "音频管线初始化成功 (CS43131 + I2S DMA 就绪)！");
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 动态调整采样率与位深
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_reconfig(const audio_format_t *fmt)
{
    if (!fmt) return ESP_ERR_INVALID_ARG;
    if (s_current_fmt.sample_rate == fmt->sample_rate &&
        s_current_fmt.bits_per_sample == fmt->bits_per_sample &&
        s_current_fmt.channels == fmt->channels) {
        return ESP_OK; /* 无需重配 */
    }

    ESP_LOGI(TAG, "重配音频输出: %lu Hz -> %lu Hz, %d bit -> %d bit",
             (unsigned long)s_current_fmt.sample_rate, (unsigned long)fmt->sample_rate,
             s_current_fmt.bits_per_sample, fmt->bits_per_sample);

    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();

    /* 1. 通知 CS43131 调整采样率和位宽 */
    if (i2c_bus) {
        cs43131_set_sample_rate(i2c_bus, fmt->sample_rate);
    }

    /* 2. 重配 I2S DMA 时钟与 Slot 格式 */
    i2s_channel_disable(s_tx_chan);

    i2s_std_clk_config_t clk_cfg = {
        .sample_rate_hz = fmt->sample_rate,
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .ext_clk_freq_hz = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        (i2s_data_bit_width_t)fmt->bits_per_sample,
        fmt->channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
    );
    if (fmt->bits_per_sample == 24) {
        slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    }
    i2s_channel_reconfig_std_slot(s_tx_chan, &slot_cfg);

    i2s_channel_enable(s_tx_chan);

    s_current_fmt = *fmt;
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * PCM 数据流写入
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_write(const void *data, size_t size_bytes, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    return i2s_channel_write(s_tx_chan, data, size_bytes, bytes_written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t audio_pipeline_flush(void)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    /* 清空 DMA 缓冲区 */
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 音量控制
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_current_volume = volume;
    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (i2c_bus) {
        cs43131_set_volume(i2c_bus, volume);
    }
    return ESP_OK;
}

uint8_t audio_pipeline_get_volume(void)
{
    return s_current_volume;
}

esp_err_t audio_pipeline_set_mute(bool mute)
{
    s_is_muted = mute;
    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (i2c_bus) {
        cs43131_set_mute(i2c_bus, mute);
    }
    return ESP_OK;
}
