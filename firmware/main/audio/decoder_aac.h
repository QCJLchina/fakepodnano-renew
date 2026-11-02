/**
 * FAKE_POD_NANO AAC / M4A 音频解码器
 * ============================================
 * 基于业界广泛验证的 libhelix-aac 定点数解码核心
 * 支持：
 *   - .aac (ADTS 帧流)
 *   - .m4a (MP4 容器中的 AAC 编码音频)
 */

#pragma once

#include "decoder_base.h"

class AacDecoder : public AudioDecoder {
public:
    AacDecoder();
    virtual ~AacDecoder();

    virtual bool open(FILE *file) override;
    virtual const audio_track_info_t* get_info() const override;
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) override;
    virtual bool seek(uint32_t target_sec) override;
    virtual void close() override;

private:
    FILE *m_file;
    void *m_hAACDecoder;
    audio_track_info_t m_info;
    uint8_t *m_in_buf;
    short *m_pcm;
    size_t m_in_buf_size;
    size_t m_in_buf_len;
    uint32_t m_data_offset;
    uint32_t m_file_size;
    uint64_t m_total_samples;
    uint64_t m_current_samples;

    bool parse_container();
};
