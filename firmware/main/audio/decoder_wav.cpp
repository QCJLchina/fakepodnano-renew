/**
 * FAKE_POD_NANO WAV 音频解码器实现
 */

#include "decoder_wav.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "WAV_DEC";

#pragma pack(push, 1)
typedef struct {
    char riff_id[4];        /* "RIFF" */
    uint32_t riff_size;
    char wave_id[4];        /* "WAVE" */
} riff_header_t;

typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
} chunk_header_t;

typedef struct {
    uint16_t audio_format;    /* 1 = PCM */
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} fmt_chunk_t;
#pragma pack(pop)

WavDecoder::WavDecoder()
    : m_file(NULL), m_data_offset(0), m_data_size(0), m_bytes_read(0), m_bytes_per_sec(0)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_WAV;
}

WavDecoder::~WavDecoder()
{
    close();
}

bool WavDecoder::open(FILE *file)
{
    if (!file) return false;
    m_file = file;
    fseek(m_file, 0, SEEK_SET);

    riff_header_t riff;
    if (fread(&riff, 1, sizeof(riff), m_file) != sizeof(riff)) {
        return false;
    }
    if (memcmp(riff.riff_id, "RIFF", 4) != 0 || memcmp(riff.wave_id, "WAVE", 4) != 0) {
        return false;
    }

    bool has_fmt = false, has_data = false;
    fmt_chunk_t fmt = {};

    /* 逐 chunk 查找 fmt 和 data */
    while (!feof(m_file)) {
        chunk_header_t ch;
        if (fread(&ch, 1, sizeof(ch), m_file) != sizeof(ch)) break;

        if (memcmp(ch.chunk_id, "fmt ", 4) == 0) {
            size_t read_sz = ch.chunk_size < sizeof(fmt) ? ch.chunk_size : sizeof(fmt);
            if (fread(&fmt, 1, read_sz, m_file) != read_sz) return false;
            if (ch.chunk_size > read_sz) {
                fseek(m_file, ch.chunk_size - read_sz, SEEK_CUR);
            }
            has_fmt = true;
        } else if (memcmp(ch.chunk_id, "data", 4) == 0) {
            m_data_offset = ftell(m_file);
            m_data_size = ch.chunk_size;
            has_data = true;
            break; /* 找到了 data 区，后续直接读取数据 */
        } else {
            /* 跳过未知 chunk */
            fseek(m_file, ch.chunk_size, SEEK_CUR);
        }
    }

    if (!has_fmt || !has_data || fmt.audio_format != 1 /* PCM */) {
        ESP_LOGE(TAG, "非标准未压缩 PCM WAV 格式");
        return false;
    }

    m_info.fmt.sample_rate = fmt.sample_rate;
    m_info.fmt.bits_per_sample = (uint8_t)fmt.bits_per_sample;
    m_info.fmt.channels = (uint8_t)fmt.num_channels;
    m_bytes_per_sec = fmt.byte_rate;
    m_info.bit_rate = fmt.byte_rate * 8 / 1000;
    m_info.total_time_sec = m_bytes_per_sec ? (m_data_size / m_bytes_per_sec) : 0;
    m_bytes_read = 0;

    ESP_LOGI(TAG, "成功打开 WAV: %lu Hz, %d bit, %d 声道, 时长: %lu 秒",
             (unsigned long)m_info.fmt.sample_rate, m_info.fmt.bits_per_sample,
             m_info.fmt.channels, (unsigned long)m_info.total_time_sec);

    return true;
}

const audio_track_info_t* WavDecoder::get_info() const
{
    return &m_info;
}

bool WavDecoder::decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes)
{
    if (!m_file || m_bytes_read >= m_data_size) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    size_t to_read = m_data_size - m_bytes_read;
    if (to_read > max_bytes) to_read = max_bytes;

    size_t r = fread(pcm_out, 1, to_read, m_file);
    m_bytes_read += r;
    if (out_bytes) *out_bytes = r;

    if (m_bytes_per_sec) {
        m_info.current_time_sec = m_bytes_read / m_bytes_per_sec;
    }
    return (r > 0);
}

bool WavDecoder::seek(uint32_t target_sec)
{
    if (!m_file || !m_bytes_per_sec) return false;

    uint32_t target_offset = target_sec * m_bytes_per_sec;
    if (target_offset > m_data_size) target_offset = m_data_size;

    fseek(m_file, m_data_offset + target_offset, SEEK_SET);
    m_bytes_read = target_offset;
    m_info.current_time_sec = target_sec;
    return true;
}

void WavDecoder::close()
{
    m_file = NULL;
    m_bytes_read = 0;
}
