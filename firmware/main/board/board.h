/**
 * FAKE_POD_NANO 板级接口声明
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 板级初始化：I2C 总线 + 硬件信息打印 + I2C 扫描
 */
esp_err_t board_init(void);

/**
 * @brief 获取共享 I2C 总线句柄（触摸/DAC/IMU 共用）
 * @note 必须先调用 board_init()
 */
i2c_master_bus_handle_t board_i2c_bus_get(void);

#ifdef __cplusplus
}
#endif
