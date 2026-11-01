/**
 * FAKE_POD_NANO USB UAC 高保真声卡服务实现
 */

#include "usb_uac_service.h"
#include "usb_device_uac.h"
#include "audio_pipeline.h"
#include "dac_cs43131.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "USB_UAC";

static uint32_t s_uac_current_sample_rate = 48000;
static bool s_uac_streaming = false;

/* ---------------------------------------------------------------
 * USB 音频扬声器输出数据接收回调
 * --------------------------------------------------------------- */
static esp_err_t uac_spk_output_cb(uint8_t *buf, size_t len, void *cb_ctx)
{
    if (!buf || len == 0) return ESP_OK;

    s_uac_streaming = true;
    size_t bytes_written = 0;
    /* 直接推流至 I2S DMA 硬件缓冲区 */
    return audio_pipeline_write(buf, len, &bytes_written, 100);
}

/* ---------------------------------------------------------------
 * Windows 系统音量与静音回调联动
 * --------------------------------------------------------------- */
static void uac_spk_set_volume_cb(uint32_t volume_raw, void *cb_ctx)
{
    /* UAC 协议中 volume 范围通常为 0..100 或 dB 表达，这里做 0..100 线性映射 */
    uint8_t vol_percent = (uint8_t)(volume_raw > 100 ? 100 : volume_raw);
    ESP_LOGI(TAG, "电脑下发音量控制: %d%%", vol_percent);
    audio_pipeline_set_volume(vol_percent);
}

static void uac_spk_set_mute_cb(uint32_t mute, void *cb_ctx)
{
    ESP_LOGI(TAG, "电脑下发静音控制: %s", mute ? "静音" : "取消静音");
    audio_pipeline_set_mute(mute != 0);
}

/* ---------------------------------------------------------------
 * 核心创新：电脑动态切换采样率回调 (突破 16bit 44.1k 瓶颈)
 * --------------------------------------------------------------- */
static void uac_sample_rate_changed_cb(uint32_t new_sample_rate, void *cb_ctx)
{
    ESP_LOGI(TAG, "电脑切换采样率 -> %lu Hz！正在无缝重配 I2S DMA 与 CS43131 DAC...", (unsigned long)new_sample_rate);
    s_uac_current_sample_rate = new_sample_rate;

    audio_format_t new_fmt = {
        .sample_rate = new_sample_rate,
        .bits_per_sample = 16, /* 默认或依据 UAC 配置 */
        .channels = 2,
    };

    /* 立即重调硬件时钟与 DAC 寄存器 */
    audio_pipeline_reconfig(&new_fmt);
}

/* ---------------------------------------------------------------
 * 服务启停控制
 * --------------------------------------------------------------- */
esp_err_t usb_uac_service_start(void)
{
    ESP_LOGI(TAG, "正在启动 USB 高保真多采样率声卡服务...");

    uac_device_config_t cfg = {};
    cfg.skip_tinyusb_init = false;
    cfg.output_cb = uac_spk_output_cb;
    cfg.input_cb = NULL; /* 纯声卡输出模式 */
    cfg.set_volume_cb = uac_spk_set_volume_cb;
    cfg.set_mute_cb = uac_spk_set_mute_cb;
    cfg.set_sample_rate_cb = uac_sample_rate_changed_cb;

    esp_err_t ret = uac_device_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uac_device_init 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB UAC 声卡服务已就绪！支持 44.1k/48k/88.2k/96k/176.4k/192k 多采样率！");
    return ESP_OK;
}

esp_err_t usb_uac_service_stop(void)
{
    s_uac_streaming = false;
    return ESP_OK;
}

bool usb_uac_is_streaming(void)
{
    return s_uac_streaming;
}

uint32_t usb_uac_get_current_sample_rate(void)
{
    return s_uac_current_sample_rate;
}
