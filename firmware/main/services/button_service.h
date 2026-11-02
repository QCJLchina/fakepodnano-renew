/**
 * FAKE_POD_NANO 物理按键服务
 * ============================================
 * 扫描用户按键（BTN_PIN=48）与电源键候选（GPIO0），支持：
 *   - 单击 / 双击 / 长按识别
 *   - 每次电平变化都打印日志，便于确认按键实际接在哪个 GPIO
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t button_service_start(void);

#ifdef __cplusplus
}
#endif
