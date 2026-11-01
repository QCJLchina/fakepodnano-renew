/**
 * FAKE_POD_NANO DSD (DoP) 解码器实现
 */

#include "decoder_dsd.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "DSD_DEC";

#pragma pack(push, 1)
typedef struct {
    char header_id[4];      /* "DSD " */
    uint64_t chunk_size;    /* 28 */
    uint64_t file_size;
    uint64_t metadata_offset;
} dsf_header_t;

typedef struct {
    char chunk_id[4];       /* "fmt " */
    uint64_t chunk_size;    /* 52 */
    uint32_t format_version;
    uint32_t format_id;     /* 0: DSD raw */
    uint32_t channel_type;  /* 1: mono, 2: stereo */
    uint32_t channel_num;   /* 1 or 2 */
    uint32_t sampling_frequency; /* 2822400 for DSD64 */
    uint32_t bits_per_sample;    /* 1 */
    uint64_t sample_count;
    uint32_t block_size_per_channel; /* 4096 */
    uint32_t reserved;
} dsf_fmt_chunk_t;

typedef struct {
    char chunk_id[4];       /* "data" */
    uint64_t chunk_size;
} dsf_data_chunk_t;
#pragma pack(pop)

DsdDecoder::DsdDecoder()
    : m_file(NULL), m_data_offset(0), m_data_size(0), m_block_size(4096),
      m_channel_num(2), m_sample_count(0), m_block_l(NULL), m_block_r(NULL),
      m_block_idx(0), m_bytes_processed(0), m_dop_marker(0x05)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_DSD_DSF;
    m_info.is_dop_dsd = true;
}

DsdDecoder::~DsdDecoder()
{
    close();
}

bool DsdDecoder::load_next_blocks()
{
    if (!m_file || m_bytes_processed >= m_data_size) {
        return false;
    }

    /* DSF 存储结构：左声道一个 block，紧接着右声道一个 block */
    size_t r1 = fread(m_block_l, 1, m_block_size, m_file);
    size_t r2 = 0;
    if (m_channel_num == 2) {
        r2 = fread(m_block_r, 1, m_block_size, m_file);
    } else {
        memcpy(m_block_r, m_block_l, m_block_size);
        r2 = r1;
    }

    if (r1 == 0) return false;

    m_bytes_processed += (r1 + r2);
    m_block_idx = 0;
    return true;
}

bool DsdDecoder::open(FILE *file)
{
    if (!file) return false;
    m_file = file;
    fseek(m_file, 0, SEEK_SET);

    dsf_header_t dsf;
    if (fread(&dsf, 1, sizeof(dsf), m_file) != sizeof(dsf)) return false;
    if (memcmp(dsf.header_id, "DSD ", 4) != 0) return false;

    dsf_fmt_chunk_t fmt;
    if (fread(&fmt, 1, sizeof(fmt), m_file) != sizeof(fmt)) return false;
    if (memcmp(fmt.chunk_id, "fmt ", 4) != 0 || fmt.bits_per_sample != 1) return false;

    dsf_data_chunk_t data_chunk;
    if (fread(&data_chunk, 1, sizeof(data_chunk), m_file) != sizeof(data_chunk)) return false;
    if (memcmp(data_chunk.chunk_id, "data", 4) != 0) return false;

    m_data_offset = ftell(m_file);
    m_data_size = data_chunk.chunk_size - sizeof(data_chunk);
    m_block_size = fmt.block_size_per_channel;
    m_channel_num = fmt.channel_num;
    m_sample_count = fmt.sample_count;

    /* 分配左右声道块缓存 */
    m_block_l = (uint8_t*)malloc(m_block_size);
    m_block_r = (uint8_t*)malloc(m_block_size);
    if (!m_block_l || !m_block_r) {
        close();
        return false;
    }

    /* DoP 标准：DSD64 (2822400) 映射为 176400 Hz / 24bit (实际使用 32-bit slot 对齐) */
    m_info.fmt.sample_rate = 176400;
    m_info.fmt.bits_per_sample = 24;
    m_info.fmt.channels = 2;
    m_info.bit_rate = 2822400 * m_channel_num / 1000;
    m_info.total_time_sec = (uint32_t)(m_sample_count / fmt.sampling_frequency);
    m_bytes_processed = 0;
    m_dop_marker = 0x05;

    /* 加载首批 blocks */
    if (!load_next_blocks()) {
        close();
        return false;
    }

    ESP_LOGI(TAG, "成功打开 DSD (DSF): 原始采样率 %lu Hz, 输出 DoP 176.4kHz/24bit, 时长: %lu 秒",
             (unsigned long)fmt.sampling_frequency, (unsigned long)m_info.total_time_sec);

    return true;
}

const audio_track_info_t* DsdDecoder::get_info() const
{
    return &m_info;
}

bool DsdDecoder::decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes)
{
    if (!m_file || !m_block_l || !m_block_r) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    /* 每个立体声 DoP 采样占用 8 字节（左右声道各 32 位 slot，24 位有效数据） */
    uint32_t *out_p = (uint32_t*)pcm_out;
    size_t samples_to_generate = max_bytes / (2 * sizeof(uint32_t));
    size_t samples_generated = 0;

    while (samples_generated < samples_to_generate) {
        if (m_block_idx + 2 > m_block_size) {
            if (!load_next_blocks()) break;
        }

        /* 每次从当前 block 中为左、右声道各提取 2 字节（16 个 DSD 比特） */
        uint16_t dsd_l = ((uint16_t)m_block_l[m_block_idx] << 8) | m_block_l[m_block_idx + 1];
        uint16_t dsd_r = ((uint16_t)m_block_r[m_block_idx] << 8) | m_block_r[m_block_idx + 1];
        m_block_idx += 2;

        /* DoP 标记 0x05 与 0xFA 交替 */
        uint32_t marker = (uint32_t)m_dop_marker << 16;
        m_dop_marker = (m_dop_marker == 0x05) ? 0xFA : 0x05;

        /* 封装为 24-bit PCM: [Marker(8-bit) | DSD_Data(16-bit)] */
        out_p[0] = marker | dsd_l;
        out_p[1] = marker | dsd_r;
        out_p += 2;
        samples_generated++;
    }

    if (out_bytes) {
        *out_bytes = samples_generated * 2 * sizeof(uint32_t);
    }

    if (m_info.fmt.sample_rate) {
        m_info.current_time_sec = (uint32_t)(m_bytes_processed * m_info.total_time_sec / (m_data_size ? m_data_size : 1));
    }

    return (samples_generated > 0);
}

bool DsdDecoder::seek(uint32_t target_sec)
{
    if (!m_file || !m_info.total_time_sec) return false;

    if (target_sec > m_info.total_time_sec) target_sec = m_info.total_time_sec;
    float ratio = (float)target_sec / (float)m_info.total_time_sec;
    uint64_t target_offset = (uint64_t)(m_data_size * ratio);
    /* 对齐到 block 大小 */
    uint32_t block_pair_size = m_block_size * m_channel_num;
    target_offset = (target_offset / block_pair_size) * block_pair_size;

    fseek(m_file, m_data_offset + target_offset, SEEK_SET);
    m_bytes_processed = target_offset;
    m_info.current_time_sec = target_sec;
    m_dop_marker = 0x05;
    return load_next_blocks();
}

void DsdDecoder::close()
{
    if (m_block_l) { free(m_block_l); m_block_l = NULL; }
    if (m_block_r) { free(m_block_r); m_block_r = NULL; }
    m_file = NULL;
    m_bytes_processed = 0;
    m_block_idx = 0;
}
