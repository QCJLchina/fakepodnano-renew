/**
 * FAKE_POD_NANO 专辑封面服务
 * ============================================
 * 解析音频文件内嵌封面（FLAC PICTURE / MP3 ID3v2 APIC），用 tjpgd 解码成
 * LVGL 真彩色图像。返回的 lv_img_dsc_t 由本模块持有，调用方不要释放。
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 取指定音频文件的内嵌封面（已解码）。
 * @param path     音频文件路径
 * @param out_dsc  成功时填入可用的 LVGL 图像描述符
 * @return true=有封面（out_dsc 有效）；false=无封面/解码失败
 */
bool album_art_get(const char *path, lv_img_dsc_t *out_dsc);

/** @brief 释放缓存的封面缓冲 */
void album_art_release(void);

#ifdef __cplusplus
}
#endif
