/**
 * FAKE_POD_NANO 桌面主页（2x2 宫格大触控卡片）声明
 * 依据 UI_DESIGN_SPEC.md：正在播放 / 音乐目录 / USB DAC / 系统设置
 */

#pragma once

#include "lvgl.h"

/**
 * @brief 创建桌面主页
 * @return 页面对象（作为 LVGL screen 使用）
 */
lv_obj_t* ui_view_home_create(void);

/**
 * @brief 周期刷新主页动态数据（时钟 / 当前曲目）
 */
void ui_view_home_update(void);
