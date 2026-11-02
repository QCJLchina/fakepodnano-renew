/**
 * FAKE_POD_NANO USB UAC 高保真声卡服务实现
 */

#include "usb_uac_service.h"
#include "usb_device_uac.h"
#include "audio_pipeline.h"

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "USB_UAC";

static uint32_t s_uac_current_sample_rate = CONFIG_UAC_SAMPLE_RATE;
static bool s_uac_streaming = false;
static bool s_uac_held = false;
static bool s_started = false;
static volatile uint64_t s_rx_bytes = 0;

static void uac_apply_rate(uint32_t sample_rate)
{
    audio_format_t fmt = {
        .sample_rate = sample_rate,
        .bits_per_sample = CONFIG_UAC_BIT_RESOLUTION,
        .channels = CONFIG_UAC_SPEAKER_CHANNEL_NUM,
    };
    if (audio_pipeline_reconfig(&fmt) != ESP_OK) {
        ESP_LOGW(TAG, "重配 I2S/DAC 失败: %lu Hz / %d bit", (unsigned long)sample_rate, CONFIG_UAC_BIT_RESOLUTION);
    }
}

/* ---------------------------------------------------------------
 * USB 音频扬声器输出数据接收回调
 * --------------------------------------------------------------- */
static esp_err_t uac_spk_output_cb(uint8_t *buf, size_t len, void *cb_ctx)
{
    (void)cb_ctx;
    if (!buf || len == 0) return ESP_OK;

    if (!s_uac_held) {
        if (audio_pipeline_acquire(AUDIO_OWNER_UAC) != ESP_OK) {
            s_uac_streaming = false;
            return ESP_ERR_INVALID_STATE;
        }
        s_uac_held = true;
        uac_apply_rate(s_uac_current_sample_rate);
    }

    s_uac_streaming = true;
    s_rx_bytes += len;
    size_t bytes_written = 0;
    esp_err_t wr = audio_pipeline_write(buf, len, &bytes_written, 20);
    static bool s_logged_first = false;
    if (!s_logged_first) {
        s_logged_first = true;
        ESP_LOGI(TAG, "收到首包音频: %u 字节 @ %lu Hz / %d bit，写入 %u 字节 (ret=%s)",
                 (unsigned)len, (unsigned long)s_uac_current_sample_rate,
                 CONFIG_UAC_BIT_RESOLUTION, (unsigned)bytes_written, esp_err_to_name(wr));
    }
    return wr;
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
static void uac_spk_active_cb(bool active, void *cb_ctx)
{
    (void)cb_ctx;
    if (active) return;
    s_uac_streaming = false;
    if (s_uac_held) {
        audio_pipeline_release(AUDIO_OWNER_UAC);
        s_uac_held = false;
    }
}

static void uac_sample_rate_changed_cb(uint32_t new_sample_rate, void *cb_ctx)
{
    (void)cb_ctx;
    ESP_LOGI(TAG, "电脑切换采样率 -> %lu Hz", (unsigned long)new_sample_rate);
    s_uac_current_sample_rate = new_sample_rate;
    if (s_uac_held) {
        uac_apply_rate(new_sample_rate);
    }
}

/* ---------------------------------------------------------------
 * 服务启停控制
 * --------------------------------------------------------------- */
esp_err_t usb_uac_service_start(void)
{
    if (s_started) {
        ESP_LOGI(TAG, "USB 声卡已在运行，忽略重复启动");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在启动 USB 高保真多采样率声卡服务...");

    uac_device_config_t cfg = {};
    cfg.skip_tinyusb_init = false;
    cfg.output_cb = uac_spk_output_cb;
    cfg.input_cb = NULL; /* 纯声卡输出模式 */
    cfg.set_volume_cb = uac_spk_set_volume_cb;
    cfg.set_mute_cb = uac_spk_set_mute_cb;
    cfg.set_sample_rate_cb = uac_sample_rate_changed_cb;
    cfg.spk_active_cb = uac_spk_active_cb;

    esp_err_t ret = uac_device_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uac_device_init 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB UAC 声卡已就绪：44.1/48/88.2/96 kHz，%d-bit 立体声", CONFIG_UAC_BIT_RESOLUTION);

    /* 启动即给一个可闻的默认硬件音量并解除静音，避免完全依赖电脑下发 */
    audio_pipeline_set_volume(80);
    audio_pipeline_set_mute(false);
    ESP_LOGI(TAG, "默认硬件音量已设置为 80%% 并解除静音");
    s_started = true;
    return ESP_OK;
}

esp_err_t usb_uac_service_stop(void)
{
    s_uac_streaming = false;
    if (s_uac_held) {
        audio_pipeline_release(AUDIO_OWNER_UAC);
        s_uac_held = false;
    }
    return ESP_OK;
}

bool usb_uac_is_streaming(void)
{
    return s_uac_streaming;
}

bool usb_uac_service_is_started(void)
{
    return s_started;
}

uint64_t usb_uac_get_rx_bytes(void)
{
    return s_rx_bytes;
}

uint32_t usb_uac_get_current_sample_rate(void)
{
    return s_uac_current_sample_rate;
}
