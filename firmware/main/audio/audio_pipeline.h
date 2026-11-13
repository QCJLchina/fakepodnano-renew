/**
 * FAKE_POD_NANO 音频输出管线 (I2S DMA + CS43131 联动)
 * ====================================================
 * 负责：
 *   - 初始化 I2S DMA 主机通道
 *   - 动态调整采样率与位深。USB 全速下最高 96 kHz / 24-bit-in-32。
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
    uint32_t sample_rate;       /**< 44100, 48000, 96000 等 */
    uint8_t  bits_per_sample;   /**< 16, 24, 32 */
    uint8_t  channels;          /**< 1(单声道) 或 2(立体声) */
} audio_format_t;

/** 同一时刻只允许一个输出者占用 I2S + CS43131 */
typedef enum {
    AUDIO_OWNER_NONE = 0,
    AUDIO_OWNER_PLAYER,
    AUDIO_OWNER_UAC,
} audio_owner_t;

/**
 * @brief 申请独占输出。已被对方占用时返回 ESP_ERR_INVALID_STATE，不会把对方踢下线。
 */
esp_err_t audio_pipeline_acquire(audio_owner_t who);

/**
 * @brief 释放输出。who 不是当前持有者时不做任何事。
 */
void audio_pipeline_release(audio_owner_t who);

audio_owner_t audio_pipeline_owner(void);

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

/**
 * @brief 设置均衡器预设
 * @param preset 0=关闭 1=流行 2=摇滚 3=古典 4=人声
 */
esp_err_t audio_pipeline_set_eq(int preset);

/** @brief 当前均衡器预设 */
int audio_pipeline_get_eq(void);

/**
 * @brief 播放一段测试正弦音（用于排查 DAC/耳放通路，不经过 USB）
 * @param freq_hz    频率，如 1000
 * @param ms         时长毫秒
 * @param volume_pct 硬件音量 0~100
 */
esp_err_t audio_pipeline_play_test_tone(uint32_t freq_hz, uint32_t ms, uint8_t volume_pct);

#ifdef __cplusplus
}
#endif
