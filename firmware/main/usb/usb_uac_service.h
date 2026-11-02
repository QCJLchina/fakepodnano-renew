/**
 * FAKE_POD_NANO USB UAC 高保真声卡服务
 * ============================================
 * 突破原固件 16bit 44.1kHz 限制！
 * 支持：
 *   - 全速 USB 下 44.1 / 48 / 88.2 / 96 kHz，24-bit（32-bit slot）立体声
 *   - 动态采样率感知与 I2S/CS43131 瞬时重配
 *   - 与本地播放互斥：正在播本地文件时拒绝 USB 推流
 *   - Windows/Mac 系统音量联动 CS43131 硬件音量
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 USB 声卡模式
 */
esp_err_t usb_uac_service_start(void);

/**
 * @brief 停止 USB 声卡模式
 */
esp_err_t usb_uac_service_stop(void);

/**
 * @brief 查询当前 USB 声卡是否已与电脑连接并正在推流
 */
bool usb_uac_is_streaming(void);

/**
 * @brief 获取当前 USB Host (Windows) 下发的实际采样率
 */
uint32_t usb_uac_get_current_sample_rate(void);

#ifdef __cplusplus
}
#endif
