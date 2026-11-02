/**
 * 自定义中文字体声明（中文 + 日文假名/汉字 + 韩文谚文）
 * 由 lv_font_conv 生成：
 *   ui_font_cjk_20.c : 20px，ASCII + 界面用到的中文
 *   ui_font_cjk_16.c : 16px，ASCII + GB2312 中文 + 假名 + 全谚文
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(ui_font_cjk_16);
LV_FONT_DECLARE(ui_font_cjk_20);

#ifdef __cplusplus
}
#endif
