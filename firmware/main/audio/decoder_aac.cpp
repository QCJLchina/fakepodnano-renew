/**
 * FAKE_POD_NANO AAC / M4A 音频解码器实现
 */

#include "decoder_aac.h"
#include "aacdec.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "AAC_DEC";

#define AAC_IN_BUF_SIZE (4096)

AacDecoder::AacDecoder()
    : m_file(NULL), m_hAACDecoder(NULL), m_in_buf(NULL), m_pcm(NULL), m_in_buf_size(AAC_IN_BUF_SIZE),
      m_in_buf_len(0), m_data_offset(0), m_file_size(0), m_total_samples(0), m_current_samples(0)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_AAC_M4A;
}

AacDecoder::~AacDecoder()
{
    close();
}

/* 解析 MP4 容器寻找 mdat，或者直接检测 ADTS 帧同步字 0xFFF */
bool AacDecoder::parse_container()
{
    uint8_t header[8];
    if (fread(header, 1, 8, m_file) != 8) return false;

    /* 检查是否为 MP4/M4A 容器 (ftyp 盒子) */
    if (memcmp(header + 4, "ftyp", 4) == 0) {
        ESP_LOGI(TAG, "检测到 M4A/MP4 容器，正在定位音频数据段 (mdat)...");
        fseek(m_file, 0, SEEK_SET);
        while (!feof(m_file)) {
            uint32_t atom_size;
            char atom_type[5] = {0};
            if (fread(&atom_size, 1, 4, m_file) != 4) break;
            if (fread(atom_type, 1, 4, m_file) != 4) break;

            /* 大端转小端 */
            atom_size = ((atom_size >> 24) & 0xFF) |
                        ((atom_size >> 8)  & 0xFF00) |
                        ((atom_size << 8)  & 0xFF0000) |
                        ((atom_size << 24) & 0xFF000000);

            if (memcmp(atom_type, "mdat", 4) == 0) {
                m_data_offset = ftell(m_file);
                ESP_LOGI(TAG, "找到 mdat 音频流起始偏移: 0x%08lX", (unsigned long)m_data_offset);
                return true;
            }
            if (atom_size <= 8) break;
            fseek(m_file, atom_size - 8, SEEK_CUR);
        }
        /* 若遍历未找到 mdat，尝试从 0 开始搜索 ADTS */
        fseek(m_file, 0, SEEK_SET);
    } else {
        /* 原生 ADTS AAC 文件 */
        fseek(m_file, 0, SEEK_SET);
        m_data_offset = 0;
    }
    return true;
}

bool AacDecoder::open(FILE *file)
{
    if (!file) return false;
    m_file = file;

    fseek(m_file, 0, SEEK_END);
    m_file_size = ftell(m_file);
    fseek(m_file, 0, SEEK_SET);

    parse_container();

    m_hAACDecoder = (void*)AACInitDecoder();
    if (!m_hAACDecoder) {
        ESP_LOGE(TAG, "AACInitDecoder 初始化失败");
        return false;
    }

    m_in_buf = (uint8_t*)malloc(m_in_buf_size);
    m_pcm = (short*)malloc(2048 * 2 * sizeof(short));
    if (!m_in_buf || !m_pcm) {
        close();
        return false;
    }

    /* Helix 只吃 ADTS。MP4 mdat 里的裸 AAC 没有同步字，不能假装打开成功。 */
    fseek(m_file, m_data_offset, SEEK_SET);
    m_in_buf_len = fread(m_in_buf, 1, m_in_buf_size, m_file);

    unsigned char *read_ptr = m_in_buf;
    int bytes_left = m_in_buf_len;

    int sync_offset = AACFindSyncWord(read_ptr, bytes_left);
    if (sync_offset < 0) {
        ESP_LOGE(TAG, "未找到 ADTS 同步字。裸 M4A 需要先解封装，本次拒绝播放");
        close();
        return false;
    }
    read_ptr += sync_offset;
    bytes_left -= sync_offset;

    int err = AACDecode((HAACDecoder)m_hAACDecoder, &read_ptr, &bytes_left, m_pcm);
    if (err != 0) {
        ESP_LOGE(TAG, "AAC 首帧解码失败: %d", err);
        close();
        return false;
    }
    AACFrameInfo frame_info;
    AACGetLastFrameInfo((HAACDecoder)m_hAACDecoder, &frame_info);
    m_info.fmt.sample_rate = frame_info.sampRateOut;
    m_info.fmt.channels = (uint8_t)frame_info.nChans;
    m_info.fmt.bits_per_sample = (uint8_t)frame_info.bitsPerSample;
    m_info.bit_rate = frame_info.bitRate / 1000;
    if (m_info.bit_rate > 0) {
        m_info.total_time_sec = (m_file_size - m_data_offset) * 8 / (m_info.bit_rate * 1000);
    }

    /* 重新归位，准备播放 */
    fseek(m_file, m_data_offset, SEEK_SET);
    m_in_buf_len = 0;
    m_current_samples = 0;

    ESP_LOGI(TAG, "成功打开 ADTS AAC: %lu Hz, %d 声道, %d bit",
             (unsigned long)m_info.fmt.sample_rate, m_info.fmt.channels,
             m_info.fmt.bits_per_sample);

    return true;
}

const audio_track_info_t* AacDecoder::get_info() const
{
    return &m_info;
}

bool AacDecoder::decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes)
{
    if (!m_file || !m_hAACDecoder || !m_in_buf) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    /* 缓冲不足时补充读取 */
    if (m_in_buf_len < AAC_IN_BUF_SIZE / 2 && !feof(m_file)) {
        size_t to_read = AAC_IN_BUF_SIZE - m_in_buf_len;
        size_t r = fread(m_in_buf + m_in_buf_len, 1, to_read, m_file);
        m_in_buf_len += r;
    }

    if (m_in_buf_len == 0) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    unsigned char *read_ptr = m_in_buf;
    int bytes_left = m_in_buf_len;

    /* 没有同步字就不要交给 Helix。留最后一个字节，避免同步字跨在两次读取中间。 */
    int sync_offset = AACFindSyncWord(read_ptr, bytes_left);
    if (sync_offset < 0) {
        if (feof(m_file)) {
            if (out_bytes) *out_bytes = 0;
            return false;
        }
        if (m_in_buf_len > 1) {
            m_in_buf[0] = m_in_buf[m_in_buf_len - 1];
            m_in_buf_len = 1;
        }
        if (out_bytes) *out_bytes = 0;
        return true;
    }
    if (sync_offset > 0) {
        read_ptr += sync_offset;
        bytes_left -= sync_offset;
    }

    int err = AACDecode((HAACDecoder)m_hAACDecoder, &read_ptr, &bytes_left, m_pcm);

    int consumed = m_in_buf_len - bytes_left;
    if (consumed > 0 && (size_t)consumed <= m_in_buf_len) {
        memmove(m_in_buf, m_in_buf + consumed, m_in_buf_len - consumed);
        m_in_buf_len -= consumed;
    }

    if (err == 0) {
        AACFrameInfo frame_info;
        AACGetLastFrameInfo((HAACDecoder)m_hAACDecoder, &frame_info);

        size_t pcm_bytes = frame_info.outputSamps * sizeof(short);
        if (pcm_bytes > max_bytes) pcm_bytes = max_bytes;
        memcpy(pcm_out, m_pcm, pcm_bytes);
        if (out_bytes) *out_bytes = pcm_bytes;

        m_current_samples += frame_info.outputSamps / frame_info.nChans;
        if (m_info.fmt.sample_rate) {
            m_info.current_time_sec = (uint32_t)(m_current_samples / m_info.fmt.sample_rate);
        }
        return true;
    }

    if (out_bytes) *out_bytes = 0;
    return false;
}

bool AacDecoder::seek(uint32_t target_sec)
{
    if (!m_file || !m_info.total_time_sec) return false;

    if (target_sec > m_info.total_time_sec) target_sec = m_info.total_time_sec;
    float ratio = (float)target_sec / (float)m_info.total_time_sec;
    uint32_t target_pos = m_data_offset + (uint32_t)((m_file_size - m_data_offset) * ratio);

    fseek(m_file, target_pos, SEEK_SET);
    m_in_buf_len = 0;
    m_info.current_time_sec = target_sec;
    if (m_info.fmt.sample_rate) {
        m_current_samples = (uint64_t)target_sec * m_info.fmt.sample_rate;
    }
    return true;
}

void AacDecoder::close()
{
    if (m_hAACDecoder) {
        AACFreeDecoder((HAACDecoder)m_hAACDecoder);
        m_hAACDecoder = NULL;
    }
    if (m_in_buf) {
        free(m_in_buf);
        m_in_buf = NULL;
    }
    if (m_pcm) {
        free(m_pcm);
        m_pcm = NULL;
    }
    m_file = NULL;
    m_in_buf_len = 0;
    m_current_samples = 0;
}
