/**
 * FAKE_POD_NANO FLAC 无损音频解码器
 * ============================================
 * 基于 dr_flac 单头文件无损解码引擎
 * 支持 16bit / 24bit / 32bit FLAC，最大 192kHz/384kHz
 */

#pragma once

#include "decoder_base.h"

class FlacDecoder : public AudioDecoder {
public:
    FlacDecoder();
    virtual ~FlacDecoder();

    virtual bool open(FILE *file) override;
    virtual const audio_track_info_t* get_info() const override;
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) override;
    virtual bool seek(uint32_t target_sec) override;
    virtual void close() override;

private:
    FILE *m_file;
    void *m_pFlac;
    audio_track_info_t m_info;
    uint64_t m_current_frame;
    uint64_t m_total_frames;
};
