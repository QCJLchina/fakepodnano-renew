/**
 * FAKE_POD_NANO TF 卡曲库
 * 挂载 SDMMC 4-bit，并在根目录挑出第一首可播放文件。
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 挂载 /sdcard。卡不在或格式不对时返回错误，不阻断其余启动。 */
esp_err_t sd_library_mount(void);

/**
 * 在根目录查找第一首 wav/flac/mp3/aac。
 * @param out_path 至少 256 字节，成功时写入 "/sdcard/文件名"。
 */
esp_err_t sd_library_first_track(char *out_path, size_t out_len);

#ifdef __cplusplus
}
#endif
