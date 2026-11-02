/**
 * FAKE_POD_NANO 本地音频播放服务
 * ============================================
 * 负责：
 *   - 后台常驻解码播放任务 (FreeRTOS Task)
 *   - 自动选择对应解码器解码
 *   - 动态同步音频管线与 DAC 采样率及位宽
 *   - 播放/暂停/停止/跳转/音量控制状态机
 */

#pragma once

#include "decoder_base.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_STATE_STOPPED = 0,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_COMPLETED,
    PLAYER_STATE_ERROR,
} player_state_t;

typedef enum {
    PLAY_MODE_REPEAT_ALL = 0,  /* 列表循环 (默认) */
    PLAY_MODE_REPEAT_ONE,      /* 单曲循环 */
    PLAY_MODE_SHUFFLE,         /* 随机播放 */
} play_mode_t;

typedef struct {
    player_state_t state;
    char current_path[256];
    audio_track_info_t track_info;
    uint32_t current_time_sec;
    uint32_t total_time_sec;
    uint8_t volume;
} player_status_t;

/**
 * @brief 初始化音频播放服务
 */
esp_err_t player_service_init(void);

/**
 * @brief 播放指定音频文件 (WAV, FLAC, MP3, AAC, M4A, DSF)
 */
esp_err_t player_play(const char *filepath);

/**
 * @brief 暂停播放
 */
esp_err_t player_pause(void);

/**
 * @brief 恢复播放
 */
esp_err_t player_resume(void);

/**
 * @brief 停止播放
 */
esp_err_t player_stop(void);

/**
 * @brief 播放当前曲目所在目录的下一首 / 上一首（到末尾循环）
 */
esp_err_t player_next(void);
esp_err_t player_prev(void);

/**
 * @brief 播放模式控制（列表循环 / 单曲循环 / 随机播放）
 */
void player_set_play_mode(play_mode_t mode);
play_mode_t player_get_play_mode(void);

/**
 * @brief 跳转到目标秒数
 */
esp_err_t player_seek(uint32_t sec);

/**
 * @brief 设置播放音量 (0~100)
 */
esp_err_t player_set_volume(uint8_t vol);

/**
 * @brief 获取当前播放状态
 */
void player_get_status(player_status_t *out_status);

#ifdef __cplusplus
}
#endif
