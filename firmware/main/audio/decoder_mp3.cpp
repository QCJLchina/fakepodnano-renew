/**
 * FAKE_POD_NANO MP3 音频解码器实现
 */

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "decoder_mp3.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "MP3_DEC";

#define IN_BUF_CAPACITY (4096)

Mp3Decoder::Mp3Decoder()
    : m_file(NULL), m_dec(NULL), m_in_buf(NULL), m_in_buf_size(IN_BUF_CAPACITY),
      m_in_buf_len(0), m_file_size(0), m_data_offset(0), m_current_sample_count(0)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_MP3;
}

Mp3Decoder::~Mp3Decoder()
{
    close();
}

bool Mp3Decoder::open(FILE *file)
{
    if (!file) return false;
    m_file = file;

    fseek(m_file, 0, SEEK_END);
    m_file_size = ftell(m_file);
    fseek(m_file, 0, SEEK_SET);

    /* 分配解码器状态结构体与流式输入缓存 */
    mp3dec_t *dec = (mp3dec_t*)malloc(sizeof(mp3dec_t));
    if (!dec) return false;
    mp3dec_init(dec);
    m_dec = (void*)dec;

    m_in_buf = (uint8_t*)malloc(m_in_buf_size);
    if (!m_in_buf) {
        free(m_dec);
        m_dec = NULL;
        return false;
    }

    /* 跳过 ID3v2 标签 */
    uint8_t id3[10];
    if (fread(id3, 1, 10, m_file) == 10 && memcmp(id3, "ID3", 3) == 0) {
        uint32_t tag_size = ((id3[6] & 0x7F) << 21) |
                            ((id3[7] & 0x7F) << 14) |
                            ((id3[8] & 0x7F) << 7)  |
                            (id3[9] & 0x7F);
        m_data_offset = 10 + tag_size;
        fseek(m_file, m_data_offset, SEEK_SET);
    } else {
        fseek(m_file, 0, SEEK_SET);
        m_data_offset = 0;
    }

    /* 试读首帧以提取采样率与声道信息 */
    m_in_buf_len = fread(m_in_buf, 1, m_in_buf_size, m_file);
    mp3dec_frame_info_t frame_info;
    short dummy_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame((mp3dec_t*)m_dec, m_in_buf, m_in_buf_len, dummy_pcm, &frame_info);

    if (samples <= 0 || frame_info.hz == 0) {
        ESP_LOGE(TAG, "无法解析 MP3 首帧");
        close();
        return false;
    }

    m_info.fmt.sample_rate = frame_info.hz;
    m_info.fmt.channels = (uint8_t)frame_info.channels;
    m_info.fmt.bits_per_sample = 16;
    m_info.bit_rate = frame_info.bitrate_kbps;
    if (frame_info.bitrate_kbps > 0) {
        m_info.total_time_sec = (m_file_size - m_data_offset) * 8 / (frame_info.bitrate_kbps * 1000);
    }
    m_current_sample_count = 0;

    /* 重新归位，准备从数据起始处开始播放 */
    fseek(m_file, m_data_offset, SEEK_SET);
    m_in_buf_len = 0;
    mp3dec_init((mp3dec_t*)m_dec);

    ESP_LOGI(TAG, "成功打开 MP3: %lu Hz, %d 声道, 码率: %lu kbps, 时长: %lu 秒",
             (unsigned long)m_info.fmt.sample_rate, m_info.fmt.channels,
             (unsigned long)m_info.bit_rate, (unsigned long)m_info.total_time_sec);

    return true;
}

const audio_track_info_t* Mp3Decoder::get_info() const
{
    return &m_info;
}

bool Mp3Decoder::decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes)
{
    if (!m_file || !m_dec || !m_in_buf) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    /* 如果输入缓冲区内的数据少于 2KB 且未到文件尾，补充读取 */
    if (m_in_buf_len < IN_BUF_CAPACITY / 2 && !feof(m_file)) {
        size_t to_read = IN_BUF_CAPACITY - m_in_buf_len;
        size_t r = fread(m_in_buf + m_in_buf_len, 1, to_read, m_file);
        m_in_buf_len += r;
    }

    if (m_in_buf_len == 0) {
        if (out_bytes) *out_bytes = 0;
        return false; /* 播放结束 */
    }

    mp3dec_frame_info_t frame_info;
    short temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame((mp3dec_t*)m_dec, m_in_buf, m_in_buf_len, temp_pcm, &frame_info);

    if (frame_info.frame_bytes > 0) {
        /* 移除已消费的数据 */
        memmove(m_in_buf, m_in_buf + frame_info.frame_bytes, m_in_buf_len - frame_info.frame_bytes);
        m_in_buf_len -= frame_info.frame_bytes;
    } else {
        /* 未成功锁定帧，前进 1 字节尝试重新对齐 */
        memmove(m_in_buf, m_in_buf + 1, m_in_buf_len - 1);
        m_in_buf_len -= 1;
        if (out_bytes) *out_bytes = 0;
        return true; /* 允许继续寻找下一帧 */
    }

    if (samples > 0) {
        size_t pcm_bytes = samples * sizeof(short) * frame_info.channels;
        if (pcm_bytes > max_bytes) pcm_bytes = max_bytes;
        memcpy(pcm_out, temp_pcm, pcm_bytes);
        if (out_bytes) *out_bytes = pcm_bytes;

        m_current_sample_count += samples;
        if (m_info.fmt.sample_rate) {
            m_info.current_time_sec = m_current_sample_count / m_info.fmt.sample_rate;
        }
        return true;
    }

    if (out_bytes) *out_bytes = 0;
    return true;
}

bool Mp3Decoder::seek(uint32_t target_sec)
{
    if (!m_file || !m_info.total_time_sec) return false;

    if (target_sec > m_info.total_time_sec) target_sec = m_info.total_time_sec;
    float ratio = (float)target_sec / (float)m_info.total_time_sec;
    uint32_t target_pos = m_data_offset + (uint32_t)((m_file_size - m_data_offset) * ratio);

    fseek(m_file, target_pos, SEEK_SET);
    m_in_buf_len = 0;
    m_info.current_time_sec = target_sec;
    if (m_info.fmt.sample_rate) {
        m_current_sample_count = target_sec * m_info.fmt.sample_rate;
    }
    return true;
}

void Mp3Decoder::close()
{
    if (m_dec) {
        free(m_dec);
        m_dec = NULL;
    }
    if (m_in_buf) {
        free(m_in_buf);
        m_in_buf = NULL;
    }
    m_file = NULL;
    m_in_buf_len = 0;
    m_current_sample_count = 0;
}
