/**
 * FAKE_POD_NANO 系统设置页
 * ============================================
 * 依据 UI_DESIGN_SPEC.md 第 4 节（基础版）：
 *   - 屏幕亮度 6 个固定档位
 *   - 自动息屏时间选择
 *   - 音量上限显示（固定 85%）
 *   - 关于本设备（下潜页）
 */

#pragma once

#include "lvgl.h"

lv_obj_t* ui_view_settings_create(void);
void ui_view_settings_update(void);
