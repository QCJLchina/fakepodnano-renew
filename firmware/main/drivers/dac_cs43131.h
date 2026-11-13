/**
 * FAKE_POD_NANO CS43131 DAC 驱动接口
 * ============================================
 * 实现见 dac_cs43131.cpp（单份实现，不要再按版本另建文件）。
 *
 * 硬件：I2S BCK=GPIO40 WS=GPIO38 DOUT=GPIO39（无 MCLK），RST=GPIO41，I2C 0x30~0x33
 *
 * 使用顺序：
 *   1. 开机：cs43131_init()            只验证控制口，不开耳放
 *   2. 首次播放 / 切采样率：cs43131_start_playback()  晶振+ASP+耳放上电（PCM 仍静音）
 *   3. 真正出声前：cs43131_release_mute()
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CS43131 I2C 地址（7 位，实际 0x30~0x33 由 ADR 决定，驱动会探测） */
#define CS43131_I2C_ADDR    0x30

/** 默认音频参数 */
#define AUDIO_SAMPLE_RATE_DEFAULT   44100
#define AUDIO_BITS_DEFAULT          32
#define AUDIO_CHANNELS_DEFAULT      2

/**
 * @brief 输出链路的槽位宽（bit）。
 *
 * 这是「I2S 槽宽」与「CS43131 ASP 位宽」的**唯一来源**：
 *   - I2S 必须按该宽度配置（32bit 槽 / Philips）
 *   - CS43131 的 ASP 常量（SAMPLE_BITS=0x04、CH1=0x07、CH2=0x0F、LCPR=0x3F）
 *     与之对应，改动任意一侧都会导致「有波形但只有底噪」。
 * 本板实测只有 32bit 槽可出声，故固定为 32，不做运行期切换。
 */
#define AUDIO_OUTPUT_SLOT_BITS      32

/** @brief 开机一次性初始化：硬件复位 + 地址探测 + DevId 校验（不开耳放/音频通路） */
esp_err_t cs43131_init(i2c_master_bus_handle_t i2c_bus);

/**
 * @brief 启动播放链路（幂等）：晶振起振 → 切 24.576MHz 直连时钟 → 32bit ASP
 *        → 耳放 pop-free 上电（PCM 仍保持静音）。采样率切换时重入即可。
 */
esp_err_t cs43131_start_playback(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate);

/** @brief 解除 PCM 静音（0x090003 ← 0xEC） */
esp_err_t cs43131_release_mute(void);

/** @brief 设置音量 0~100（映射为 PCM 数字衰减，唯一音量入口） */
esp_err_t cs43131_set_volume(i2c_master_bus_handle_t i2c_bus, uint8_t volume);

/** @brief 静音控制（唯一允许写 PCM_PATH_CTL_1 的接口，保留软斜坡配置位） */
esp_err_t cs43131_set_mute(i2c_master_bus_handle_t i2c_bus, bool mute);

#ifdef __cplusplus
}
#endif
