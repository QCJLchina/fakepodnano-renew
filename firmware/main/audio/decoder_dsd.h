/**
 * FAKE_POD_NANO DSD (DoP) 解码器
 * ============================================
 * 支持 .dsf 文件 (DSD64, 2.8224MHz)
 * 采用 DoP (DSD over PCM) 协议标准：
 *   - 以 176.4kHz / 24bit PCM 格式通过 I2S 输出
 *   - 高字节填充 0x05 / 0xFA 交替 DSD 标记
 *   - 低 16 位为纯原生 1-bit DSD 流
 *   - CS43131 硬件自动锁定并开启原生 DSD 转换
 */

#pragma once

#include "decoder_base.h"

class DsdDecoder : public AudioDecoder {
public:
    DsdDecoder();
    virtual ~DsdDecoder();

    virtual bool open(FILE *file) override;
    virtual const audio_track_info_t* get_info() const override;
    virtual bool decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes) override;
    virtual bool seek(uint32_t target_sec) override;
    virtual void close() override;

private:
    FILE *m_file;
    audio_track_info_t m_info;
    uint32_t m_data_offset;
    uint64_t m_data_size;
    uint32_t m_block_size;
    uint32_t m_channel_num;
    uint64_t m_sample_count;

    uint8_t *m_block_l;
    uint8_t *m_block_r;
    uint32_t m_block_idx;
    uint64_t m_bytes_processed;

    uint8_t m_dop_marker;

    bool load_next_blocks();
};
