/**
 * FAKE_POD_NANO 显示屏驱动 — CO5300 QSPI AMOLED
 * ============================================
 * 屏幕：鱼鹰光电 AM200Q460460LK (2.0", 460×460)
 *   有效显示区 35.88mm × 35.88mm，点距 0.078mm → 约 326 PPI
 *   刷新率：CO5300(ICNA3311) 手册上限 60Hz
 * 驱动 IC：CO5300
 * 接口：QSPI (4-bit)
 *
 * 引脚（HIFI 版）：
 *   RST=7  CS=8  SCK=9  D0=10  D1=11  D2=12  D3=13  TE=6
 *
 * 初始化序列来自 OSHWHub 官方工程 main.c + 原厂 init code，已实测可用
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 屏幕分辨率 */
#define LCD_H_RES   460
#define LCD_V_RES   460

/** 屏幕显示偏移（必须设置，否则画面偏左） */
#define LCD_X_GAP   10
#define LCD_Y_GAP   0

/**
 * @brief 初始化显示屏（QSPI 总线 + CO5300 面板）
 *
 * @param[out] ret_panel  返回面板句柄，用于后续绘制
 * @param[out] ret_io     返回 IO 句柄
 * @return ESP_OK 成功
 */
esp_err_t lcd_init(esp_lcd_panel_handle_t *ret_panel,
                   esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief 设置屏幕亮度
 * @param panel 面板句柄
 * @param brightness 0~255
 */
esp_err_t lcd_set_brightness(esp_lcd_panel_handle_t panel, uint8_t brightness);

/**
 * @brief 显示测试图案（验证屏幕是否正常工作）
 *        线性渐变 + 四角色块，便于检查色彩和偏移
 */
esp_err_t lcd_show_test_pattern(esp_lcd_panel_handle_t panel);

/* ==================================================================
 * 刷新同步 API（解决花屏 / 撕裂）
 * ================================================================== */

/**
 * @brief 同步式绘制：提交一块区域，并【等待 QSPI DMA 真正传输完成】再返回
 *
 * esp_lcd_panel_draw_bitmap() 是异步的（提交即返回）。IDF 文档要求 color 缓冲区的
 * 复用时刻必须在 on_color_trans_done 之后；直接紧接着调用 lv_disp_flush_ready()
 * 在「PSRAM 缓冲 + psram_dma_direct=0」时因为内部会 memcpy 而侥幸安全，
 * 但换成内部 DMA 内存或打开 psram_dma_direct 就是真竞态。本函数按契约做对。
 *
 * @param x_start,y_start  起始坐标（含）
 * @param x_end,y_end      结束坐标（不含）
 * @param color_data       RGB565 像素数据
 * @param timeout_ms       等待 DMA 完成的超时
 * @return ESP_OK / ESP_ERR_INVALID_STATE（未初始化）/ ESP_ERR_TIMEOUT / 其他
 */
esp_err_t lcd_draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end,
                               const void *color_data, uint32_t timeout_ms);

/**
 * @brief 等待下一个 TE（vblank）上升沿，用于把写 GRAM 的时刻对齐到消隐期
 *
 * 连续超时会自动降级（关闭 TE 同步），因此可以无条件调用：
 * TE 没接线时行为退化为"不等"，不会卡死。
 *
 * @param timeout_ms 最长等待时间，建议 LCD_TE_TIMEOUT_MS (20ms)
 * @return ESP_OK 等到边沿；ESP_ERR_TIMEOUT 超时；ESP_ERR_NOT_SUPPORTED 未启用
 */
esp_err_t lcd_wait_te(uint32_t timeout_ms);

/** @brief TE 同步当前是否生效（可能因连续超时被自动关闭） */
bool lcd_te_sync_enabled(void);

/** @brief 运行时开关 TE 同步 */
esp_err_t lcd_te_sync_set(bool enable);

#ifdef __cplusplus
}
#endif
