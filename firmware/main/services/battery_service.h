/**
 * FAKE_POD_NANO 电池电量检测（BAT_ADC = GPIO1 / ADC1_CH0）
 * 注意：分压比按常见 1/2 估算，仅用于界面显示参考。
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t battery_service_init(void);

/** 返回 0~100 的电量百分比；读取失败返回 -1 */
int battery_percent(void);

#ifdef __cplusplus
}
#endif
