/**
 * FAKE_POD_NANO 音乐目录页（TF 卡文件夹浏览）
 * ============================================
 * 依据 UI_DESIGN_SPEC.md 第 3 节：
 *   - 左上角返回（子目录显示 ‹，根目录显示 🏠）
 *   - 面包屑路径
 *   - 文件夹行 / 音频文件行（点击播放）
 */

#pragma once

#include "lvgl.h"

/** 创建页面（只创建一次，随 LVGL 生命周期常驻） */
lv_obj_t* ui_view_files_create(void);

/** 进入页面：重置到 /sdcard 根目录并刷新列表 */
void ui_view_files_enter(void);

/** 周期刷新（本页无需动态数据，保留空实现） */
void ui_view_files_update(void);
