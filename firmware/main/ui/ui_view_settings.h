/**
 * FAKE_POD_NANO 系统设置页 / 关于本设备页
 * ============================================
 * 设置页：屏幕亮度 6 档 / 均衡器 5 预设 / 自动息屏 4 档 / IMU 手势开关 /
 *         防爆耳音量限额 85% / 系统时钟同步 / 关于本设备入口。
 * 关于页：独立 screen（sim 渲染器会自动识别并单独渲染 -View about）。
 *
 * 公开函数名保持不变：
 *   ui_view_settings_create / ui_view_settings_update
 * 新增（供 sim 单独渲染关于页）：
 *   ui_view_settings_about_create / ui_view_settings_about_update
 */

#pragma once

#include "lvgl.h"

lv_obj_t* ui_view_settings_create(void);
void ui_view_settings_update(void);

lv_obj_t* ui_view_settings_about_create(void);
void ui_view_settings_about_update(void);
