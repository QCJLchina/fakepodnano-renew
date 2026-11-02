/**
 * FAKE_POD_NANO CS43131 DAC 驱动
 * ============================================
 * CS43131 是 Cirrus Logic 的高性能 DAC，支持：
 *   - PCM 最大 32bit / 384kHz
 *   - DSD64 / DSD128 / DSD256 原生解码
 *   - I2C 控制（地址 0x30）
 *
 * 硬件连接：
 *   I2S:  BCK=40  WS=38  DOUT=39   MCK=未接
 *   RST:  GPIO 41 (低电平复位)
 *   I2C:  地址 0x30，与触摸/IMU 共用总线 0
 *
 * 实现策略：
 *   1. 先通过 I2C 读芯片 ID 确认通信正常
 *   2. 写初始化寄存器序列
 *   3. I2S 输出 PCM 数据
 *
 * 注意：CS43131 寄存器映射需参考官方数据手册
 *      本实现基于公开资料推导，需实测验证
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CS43131 I2C 地址（7 位） */
#define CS43131_I2C_ADDR    0x30

/** 音频参数 */
#define AUDIO_SAMPLE_RATE_DEFAULT   48000
#define AUDIO_BITS_DEFAULT          16
#define AUDIO_CHANNELS_DEFAULT      2

/**
 * @brief 初始化 CS43131 DAC
 *
 * @param i2c_bus  已初始化的 I2C 总线句柄（来自 board_i2c_bus_get()）
 * @return ESP_OK 成功
 */
esp_err_t cs43131_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief 读取 CS43131 芯片 ID（用于验证 I2C 通信）
 * @param[out] id 读到的 ID
 */
esp_err_t cs43131_read_chip_id(i2c_master_bus_handle_t i2c_bus, uint8_t *id);

/**
 * @brief 设置采样率
 * @param sample_rate 采样率（44100/48000/96000 等）
 */
esp_err_t cs43131_set_sample_rate(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate);

/**
 * @brief 设置 PCM 字长，并同步 ASP 槽宽（16 对 16，24/32 对 32）
 */
esp_err_t cs43131_set_sample_bits(i2c_master_bus_handle_t i2c_bus, uint8_t bits);

/**
 * @brief 设置音量（硬件音量控制）
 * @param volume 0~100
 */
esp_err_t cs43131_set_volume(i2c_master_bus_handle_t i2c_bus, uint8_t volume);

/**
 * @brief 静音控制
 */
esp_err_t cs43131_set_mute(i2c_master_bus_handle_t i2c_bus, bool mute);

/**
 * @brief 硬件复位（拉低 GPIO41）
 */
esp_err_t cs43131_hw_reset(void);

#ifdef __cplusplus
}
#endif
