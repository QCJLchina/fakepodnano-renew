/**
 * FAKE_POD_NANO UI 设计令牌与通用控件（UI theme / design tokens）
 * ==============================================================
 * 唯一视觉基准：prototype/index.html（460x460 CSS 原型）。
 * 所有颜色 / 圆角 / 间距 / 字号都从原型 CSS 逐条摘出，写在下面，
 * 各页面（home/player/files/settings/uac）只允许用这里的常量与控件，
 * 不允许在页面里再手写十六进制颜色或魔法数字，否则很容易又跑偏。
 *
 * 设计基准（对应 design_ref 目录下 460x460 截图 launcher/player/files/uac/settings）：
 *   页面内边距        左右 18px / 上下 14px
 *   主页状态栏        高 24px，下方留 8px
 *   主页宫格          2x2，格子 205x183，间距 14px，起点 (18, 46)
 *   卡片              圆角 24，1px 描边，渐变 #1F2025 -> #141418
 *   图标底板          44x44，圆角 14，渐变；图标 26x26 白色
 *   列表卡片          背景 #18191D，圆角 18，描边 1px
 *   进度/滑条槽       #2C2C2E，圆角全圆
 *   强调色            #FF9F0A（金黄），辅助青 #00F2FE，绿 #30D158
 *
 * 字体说明（受现有字体资源限制，见 IMPLEMENTATION_STATUS.md）：
 *   中文字体只有 16px / 20px 两档，因此中文正文用 CJK_S(16)，标题用 CJK_M(20)；
 *   纯数字/拉丁文字用 Montserrat 12/16/20/24/32/48 与原设计逐档对应。
 */

#pragma once

#include "lvgl.h"
#include "ui_fonts.h"
#include "ui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 颜色令牌（均为原型 CSS 的实心近似值）
 * ------------------------------------------------------------------ */
#define UI_COL_BG             lv_color_hex(0x000000)  /* 纯黑，AMOLED 省电 */
#define UI_COL_ACCENT         lv_color_hex(0xFF9F0A)  /* --accent */
#define UI_COL_ACCENT_SOFT    lv_color_hex(0x3A2A0B)  /* rgba(255,159,10,.12) 叠黑 */
#define UI_COL_ACCENT_LINE    lv_color_hex(0x6E4A0C)  /* rgba(255,159,10,.28) 叠黑 */
#define UI_COL_CYAN           lv_color_hex(0x00F2FE)
#define UI_COL_CYAN_DEEP      lv_color_hex(0x4FACFE)
#define UI_COL_GREEN          lv_color_hex(0x30D158)
#define UI_COL_GREEN_SOFT     lv_color_hex(0x0D2A14)
#define UI_COL_GREEN_LINE     lv_color_hex(0x1E5C2E)

#define UI_COL_TEXT           lv_color_hex(0xFFFFFF)  /* #fff 主文字 */
#define UI_COL_TEXT2          lv_color_hex(0x8E8E93)  /* 次级 #8e8e93 */
#define UI_COL_TEXT3          lv_color_hex(0x636366)  /* 三级 #636366 */

#define UI_COL_CARD_TOP       lv_color_hex(0x1F2025)  /* 卡片渐变起 */
#define UI_COL_CARD_BOT       lv_color_hex(0x141418)  /* 卡片渐变止 */
#define UI_COL_CARD_LINE      lv_color_hex(0x2B2C31)  /* 卡片 1px 描边 */
#define UI_COL_SURFACE        lv_color_hex(0x18191D)  /* 列表/设置卡片底 */
#define UI_COL_SURFACE_LINE   lv_color_hex(0x2A2B30)  /* 列表/设置卡片描边 */
#define UI_COL_TRACK          lv_color_hex(0x2C2C2E)  /* 进度条/滑条槽 */
#define UI_COL_OPTION         lv_color_hex(0x24252A)  /* 档位按钮底 */
#define UI_COL_PILL           lv_color_hex(0x1C1D21)  /* rgba(255,255,255,.08) 叠黑 */
#define UI_COL_PRESSED        lv_color_hex(0x2A2B33)  /* 按下态提亮 */

/* 四张宫格卡片的图标底板渐变（原型 .card-* .card-icon） */
#define UI_GRAD_PLAYING_A     lv_color_hex(0xFF9F0A)
#define UI_GRAD_PLAYING_B     lv_color_hex(0xFF6B00)
#define UI_GRAD_FILES_A       lv_color_hex(0x0A84FF)
#define UI_GRAD_FILES_B       lv_color_hex(0x0055FF)
#define UI_GRAD_UAC_A         lv_color_hex(0x30D158)
#define UI_GRAD_UAC_B         lv_color_hex(0x00B0FF)
#define UI_GRAD_SETTINGS_A    lv_color_hex(0xBF5AF2)
#define UI_GRAD_SETTINGS_B    lv_color_hex(0x5E5CE6)

/* ------------------------------------------------------------------
 * 字体令牌
 * ------------------------------------------------------------------ */
#define UI_FONT_TINY          (&lv_font_montserrat_12)  /* 11~12px 拉丁小字 */
#define UI_FONT_SM            (&lv_font_montserrat_16)  /* 13~16px 拉丁 */
#define UI_FONT_MD            (&lv_font_montserrat_20)  /* 17~20px 拉丁 */
#define UI_FONT_LG            (&lv_font_montserrat_24)  /* 24px 拉丁 */
#define UI_FONT_XL            (&lv_font_montserrat_32)
#define UI_FONT_HUGE          (&lv_font_montserrat_48)
#define UI_FONT_CJK_S         (&ui_font_cjk_16)         /* 中文正文 */
#define UI_FONT_CJK_M         (&ui_font_cjk_20)         /* 中文标题 */

/* ------------------------------------------------------------------
 * 尺寸令牌
 * ------------------------------------------------------------------ */
#define UI_SCR_W              460
#define UI_SCR_H              460
#define UI_PAD_X              18   /* 页面左右内边距 */
#define UI_PAD_Y              14   /* 页面上下内边距 */
#define UI_STATUS_H           24   /* 状态栏高度 */
#define UI_STATUS_GAP         8    /* 状态栏到宫格的间距 */
#define UI_GRID_X             18
#define UI_GRID_Y             46   /* 14 + 24 + 8 */
#define UI_CARD_W             205
#define UI_CARD_H             183
#define UI_CARD_GAP           14
#define UI_CARD_RADIUS        24
#define UI_CARD_PAD           16
#define UI_ICON_TILE          44
#define UI_ICON_TILE_RADIUS   14
#define UI_SURFACE_RADIUS     18
#define UI_LIST_GAP           8
#define UI_HINT_W             60
#define UI_HINT_H             4

/* ------------------------------------------------------------------
 * 页面骨架
 * ------------------------------------------------------------------ */

/** 纯黑全屏 screen（460x460，无滚动/无边框/无内边距） */
lv_obj_t* ui_screen_create(void);

/**
 * 标准页面容器（对应原型 .view-page）
 * 返回一个 460x460 的黑底 obj（不是 screen，挂到 screen 上用），
 * 内边距 18/14，纵向 flex，禁止滚动。
 */
lv_obj_t* ui_page_create(lv_obj_t *parent);

/** 底部手势提示条（60x4 圆角，居中，距底 6px） */
lv_obj_t* ui_gesture_hint_create(lv_obj_t *parent);

/* ------------------------------------------------------------------
 * 基础控件
 * ------------------------------------------------------------------ */

/** 文本标签（默认左对齐、不换行、透明背景） */
lv_obj_t* ui_label_create(lv_obj_t *parent, const char *txt,
                          const lv_font_t *font, lv_color_t color);

/** 把标签改成右对齐（用于状态栏右侧一组信息） */
void ui_label_align_right(lv_obj_t *lbl);

/** 磨砂大卡片（渐变 #1F2025->#141418、圆角 24、1px 描边、按下高亮） */
lv_obj_t* ui_card_create(lv_obj_t *parent, int x, int y, int w, int h);

/** 深色平面卡片（列表行 / 设置卡片：#18191D、圆角 18、1px 描边） */
lv_obj_t* ui_surface_create(lv_obj_t *parent, int x, int y, int w, int h, int radius);

/** 圆角药丸底（导航返回、徽标等） */
lv_obj_t* ui_pill_create(lv_obj_t *parent, int x, int y, int w, int h,
                         lv_color_t bg, lv_color_t border);

/** 垂直滚动列表容器（透明、无滚动条、行间距 gap） */
lv_obj_t* ui_scroll_column_create(lv_obj_t *parent, int x, int y, int w, int h, int gap);

/** 给任意 obj 增加"按下变亮/描边强调"的点击反馈 */
void ui_press_feedback(lv_obj_t *obj);

/* ------------------------------------------------------------------
 * 图标
 * ------------------------------------------------------------------ */

/**
 * 图标底板：渐变色圆角方块 + 居中白色图标。
 * c1/c2 为渐变起止色，size/radius 默认 44/14。
 */
lv_obj_t* ui_icon_tile_create(lv_obj_t *parent, int x, int y, int size, int radius,
                              const lv_img_dsc_t *icon, lv_color_t c1, lv_color_t c2);

/** 纯图标（白色字形，用 recolor 上色；color 传 UI_COL_TEXT 即白色） */
lv_obj_t* ui_icon_create(lv_obj_t *parent, int x, int y,
                         const lv_img_dsc_t *icon, lv_color_t color);

/* ------------------------------------------------------------------
 * 复合控件
 * ------------------------------------------------------------------ */

/**
 * 主页 2x2 宫格大卡片（原型 .launcher-card）
 * 自动生成：渐变底 205x183 + 44x44 图标底板 + 底部标题/副标题。
 * @param out_desc  可选，返回副标题 label 指针（用于动态刷新曲目名）
 */
lv_obj_t* ui_launcher_card_create(lv_obj_t *parent, int x, int y,
                                  const lv_img_dsc_t *icon,
                                  lv_color_t grad_a, lv_color_t grad_b,
                                  const char *title, const char *desc,
                                  lv_event_cb_t on_click, lv_obj_t **out_desc);

/**
 * 主页顶部状态栏（左侧时间，右侧 CS43131 / VOL xx% / ⚡ xx%）
 * @param out_clock 返回时间 label
 * @param out_vol   返回 "VOL xx%" label
 * @param out_bat   返回电池 label
 */
lv_obj_t* ui_home_status_bar_create(lv_obj_t *parent,
                                    lv_obj_t **out_clock,
                                    lv_obj_t **out_vol,
                                    lv_obj_t **out_bat);

/**
 * 子页面导航头：左侧"返回"药丸（含 home 图标 + 文字）+ 标题。
 * 返回导航头容器；标题 label 通过 out_title 返回（可为 NULL）。
 */
lv_obj_t* ui_nav_header_create(lv_obj_t *parent, int y,
                               const char *back_text, const char *title,
                               lv_event_cb_t back_cb, lv_obj_t **out_title);

/**
 * 档位按钮（设置页 6 档亮度 / 自动息屏档位等）
 * @param active 是否高亮（强调色实心黑字）
 */
lv_obj_t* ui_option_pill_create(lv_obj_t *parent, int x, int y, int w, int h,
                                const char *txt, bool active,
                                lv_event_cb_t on_click);

/** 把档位按钮切到高亮/非高亮外观 */
void ui_option_pill_set_active(lv_obj_t *pill, bool active);

/**
 * 圆形按钮（播放页上一曲/播放暂停/下一曲）
 * @param d 直径（原型：副键 58，主键 72）
 */
lv_obj_t* ui_round_button_create(lv_obj_t *parent, int x, int y, int d,
                                 const lv_img_dsc_t *icon, lv_color_t icon_color,
                                 lv_color_t bg, lv_event_cb_t cb,
                                 lv_obj_t **out_icon);

/**
 * 小徽标药丸（如播放页 "FLAC 96kHz 24bit"、声卡页 "USB DAC 384k/32bit"）
 * 原型：11px 粗体、带同色 30% 描边和 12% 底。
 */
lv_obj_t* ui_badge_create(lv_obj_t *parent, int x, int y, const char *txt,
                          lv_color_t fg, lv_color_t bg, lv_color_t border,
                          int h, lv_obj_t **out_label);

/**
 * 细进度条（滑块可选）。返回 bg 对象；fill 与 knob 通过 out 参数返回。
 * 原型：槽 6px 圆角 #2C2C2E，填充强调色，滑块白色 16px 圆点。
 */
lv_obj_t* ui_progress_create(lv_obj_t *parent, int x, int y, int w,
                             lv_obj_t **out_fill, lv_obj_t **out_knob);

/** 设置进度条填充比例 0~100（同步移动滑块） */
void ui_progress_set_percent(lv_obj_t *fill, lv_obj_t *knob, int percent);

#ifdef __cplusplus
}
#endif
