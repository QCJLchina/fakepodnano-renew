/**
 * FAKE_POD_NANO 全新 UI 管理器 (基于 LVGL 8.3+)
 * ============================================
 * 针对 460×460 AMOLED 正方形高彩屏定制：
 *   - PSRAM 双缓冲流畅渲染
 *   - 页面状态机与动画过渡
 *   - 播放器主界面 (Player View)
 *   - 高清 USB 声卡专属界面 (UAC View)
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_VIEW_PLAYER = 0,
    UI_VIEW_UAC,
    UI_VIEW_FILES,       /* 音乐目录（TF 卡浏览） */
    UI_VIEW_SETTINGS,
    UI_VIEW_USB_MODE,    /* USB 模式选择（DAC / U盘） */
    UI_VIEW_HOME,        /* 桌面主页（2x2 宫格），开机默认 */
} ui_view_type_t;

/**
 * @brief 初始化 UI 系统（LVGL 显示驱动 + CST820 触摸驱动）
 * @param panel 面板句柄
 * @param io    面板 IO 句柄
 */
esp_err_t ui_manager_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io);

/**
 * @brief 页面切换
 */
void ui_switch_view(ui_view_type_t view);

/**
 * @brief 线程安全保护
 */
bool ui_lock(uint32_t timeout_ms);
void ui_unlock(void);

/**
 * @brief 触摸是否初始化成功（便于无串口时在界面上显示状态）
 */
bool ui_touch_ok(void);

/* 背光亮度 0~100 */
void ui_set_backlight_pct(int pct);
int  ui_get_backlight_pct(void);

/* 自动息屏秒数，0=永不 */
void ui_set_auto_off_sec(int sec);
int  ui_get_auto_off_sec(void);

/* 当前前台页面 */
ui_view_type_t ui_get_current_view(void);

#ifdef __cplusplus
}
#endif
