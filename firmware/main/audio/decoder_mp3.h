/**
 * FAKE_POD_NANO MP3 音频解码器
 * ============================================
 * 基于 minimp3 轻量高效单头文件解码库
 */

#pragma once

#include "decoder_base.h"

class Mp3Decoder : public AudioDecoder {
public:
    Mp3Decoder();
    virtual ~Mp3Decoder();

    virtual bool open(FILE *file) override;
    virtual const audio_track_info_t* get_info() const override;
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) override;
    virtual bool seek(uint32_t target_sec) override;
    virtual void close() override;

private:
    FILE *m_file;
    void *m_dec;
    audio_track_info_t m_info;
    uint8_t *m_in_buf;
    size_t m_in_buf_size;
    size_t m_in_buf_len;
    uint32_t m_file_size;
    uint32_t m_data_offset;
    uint32_t m_current_sample_count;
};
