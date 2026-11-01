/**
 * FAKE_POD_NANO 音频输出管线 (I2S DMA + CS43131 联动)
 * ====================================================
 * 负责：
 *   - 初始化 I2S DMA 主机通道
 *   - 动态调整采样率 (32k~384k) 与位深 (16/24/32bit)
 *   - 统筹 CS43131 与 ESP32 I2S 硬件时钟同步
 *   - 提供流式 PCM 写入接口，具备 DMA 缓冲与零拷贝机制
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 音频流格式定义 */
typedef struct {
    uint32_t sample_rate;       /**< 44100, 48000, 96000, 192000 等 */
    uint8_t  bits_per_sample;   /**< 16, 24, 32 */
    uint8_t  channels;          /**< 1(单声道) 或 2(立体声) */
} audio_format_t;

/**
 * @brief 初始化整个音频输出管线（I2S + CS43131）
 * @return ESP_OK 成功
 */
esp_err_t audio_pipeline_init(void);

/**
 * @brief 重新配置输出管线的格式（采样率、位深、声道）
 *        同时更新 I2S 与 CS43131 的寄存器
 */
esp_err_t audio_pipeline_reconfig(const audio_format_t *fmt);

/**
 * @brief 向音频 DMA 写入 PCM 数据（阻塞写入或超时返回）
 *
 * @param data          PCM 数据指针
 * @param size_bytes    待写入字节数
 * @param bytes_written 实际写入字节数
 * @param timeout_ms    超时毫秒数
 */
esp_err_t audio_pipeline_write(const void *data, size_t size_bytes, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief 清空当前 DMA 缓冲区并停止输出
 */
esp_err_t audio_pipeline_flush(void);

/**
 * @brief 设置整体系统音量 (0~100)
 */
esp_err_t audio_pipeline_set_volume(uint8_t volume);

/**
 * @brief 获取当前设置的音量 (0~100)
 */
uint8_t audio_pipeline_get_volume(void);

/**
 * @brief 设置静音
 */
esp_err_t audio_pipeline_set_mute(bool mute);

#ifdef __cplusplus
}
#endif
