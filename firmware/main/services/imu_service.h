/**
 * FAKE_POD_NANO IMU 手势服务（QMI8658，I2C 0x6B）
 * ============================================
 * - 读取三轴加速度
 * - 手势：左右倾 → 上一曲/下一曲；上下倾 → 音量 ±；双击(敲击) → 播放/暂停
 * - 定期打印加速度原始值，便于根据实际摆放方向校准
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_service_start(void);

/** 开启/关闭手势识别（关闭后只保留芯片检测，不识别手势） */
void imu_service_set_enabled(bool enabled);
bool imu_service_get_enabled(void);

/** 是否检测到 QMI8658 芯片（WHO_AM_I 读取成功） */
bool imu_service_present(void);

#ifdef __cplusplus
}
#endif
