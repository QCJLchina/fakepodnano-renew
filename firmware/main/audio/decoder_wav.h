/**
 * FAKE_POD_NANO WAV 音频解码器
 */

#pragma once

#include "decoder_base.h"

class WavDecoder : public AudioDecoder {
public:
    WavDecoder();
    virtual ~WavDecoder();

    virtual bool open(FILE *file) override;
    virtual const audio_track_info_t* get_info() const override;
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) override;
    virtual bool seek(uint32_t target_sec) override;
    virtual void close() override;

private:
    FILE *m_file;
    audio_track_info_t m_info;
    uint32_t m_data_offset;
    uint32_t m_data_size;
    uint32_t m_bytes_read;
    uint32_t m_bytes_per_sec;
};
