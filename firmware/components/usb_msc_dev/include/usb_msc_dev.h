/**
 * FAKE_POD_NANO USB U盘(MSC)模式
 * 仅在 CONFIG_FAKEPOD_USB_MSC_MODE=y 的固件变体中生效。
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 SD 卡并把设备作为纯 USB MSC（U盘）暴露 */
esp_err_t usb_msc_mode_start(void);

#ifdef __cplusplus
}
#endif
