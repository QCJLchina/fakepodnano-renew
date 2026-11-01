/**
 * FAKE_POD_NANO 音频解码器基类 / 统一接口
 * ============================================
 * 所有格式解码器（WAV, FLAC, MP3, AAC-M4A, DSD-DoP）遵循此统一接口
 */

#pragma once

#include "audio_pipeline.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DECODER_TYPE_UNKNOWN = 0,
    DECODER_TYPE_WAV,
    DECODER_TYPE_FLAC,
    DECODER_TYPE_MP3,
    DECODER_TYPE_AAC_M4A,
    DECODER_TYPE_DSD_DSF,
} audio_type_t;

typedef struct {
    audio_type_t type;
    audio_format_t fmt;
    uint32_t total_time_sec;
    uint32_t current_time_sec;
    uint32_t bit_rate;          /* 码率 kbps */
    bool is_dop_dsd;            /* 是否为 DoP DSD 模式 */
} audio_track_info_t;

class AudioDecoder {
public:
    virtual ~AudioDecoder() {}

    /** 打开音频文件进行解析 */
    virtual bool open(FILE *file) = 0;

    /** 获取音频基本元数据与输出格式 */
    virtual const audio_track_info_t* get_info() const = 0;

    /**
     * @brief 解码一帧并输出 PCM 数据到输出缓冲区
     * @param pcm_out      输出缓冲区指针
     * @param max_bytes    缓冲区容量
     * @param out_bytes    实际解码输出的字节数
     * @return true 正常解码; false 已到文件末尾或发生解码错误
     */
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) = 0;

    /** 跳转到指定秒数 */
    virtual bool seek(uint32_t target_sec) = 0;

    /** 关闭解码器 */
    virtual void close() = 0;
};

/** 根据文件名后缀或文件魔数自动创建匹配的解码器 */
AudioDecoder* audio_decoder_create_for_file(const char *filepath, FILE *f);

#ifdef __cplusplus
}
#endif
