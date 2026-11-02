/**
 * FAKE_POD_NANO USB 模式选择页
 * 桌面「USB 模式」卡片进入，二选一：USB DAC 声卡 / U盘(MSC)模式
 */

#pragma once

#include "lvgl.h"

lv_obj_t* ui_view_usbmode_create(void);
void ui_view_usbmode_update(void);
