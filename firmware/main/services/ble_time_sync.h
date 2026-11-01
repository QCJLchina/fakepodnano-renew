/**
 * FAKE_POD_NANO BLE 低功耗蓝牙无线对时服务
 * ============================================
 * 与手机网页端 Web Bluetooth 配套使用
 * Service UUID: 0xFFE0
 * Characteristic UUID: 0xFFE1
 * 接收来自手机网页的时间包并写入 ESP32-S3 硬件 RTC
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 BLE 无线对时服务与广播
 */
esp_err_t ble_time_sync_start(void);

/**
 * @brief 停止 BLE 广播
 */
esp_err_t ble_time_sync_stop(void);

/**
 * @brief 查询自开机以来是否已经成功对时
 */
bool ble_time_is_synced(void);

#ifdef __cplusplus
}
#endif
