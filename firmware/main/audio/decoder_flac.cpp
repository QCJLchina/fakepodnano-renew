/**
 * FAKE_POD_NANO FLAC 解码器实现
 */

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"

#include "decoder_flac.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "FLAC_DEC";

static size_t flac_read_cb(void* pUserData, void* pBufferOut, size_t bytesToRead)
{
    FILE *f = (FILE*)pUserData;
    return fread(pBufferOut, 1, bytesToRead, f);
}

static drflac_bool32 flac_seek_cb(void* pUserData, int offset, drflac_seek_origin origin)
{
    FILE *f = (FILE*)pUserData;
    int stdio_origin = SEEK_SET;
    if (origin == DRFLAC_SEEK_CUR) {
        stdio_origin = SEEK_CUR;
    } else if (origin == DRFLAC_SEEK_END) {
        stdio_origin = SEEK_END;
    }
    return (fseek(f, offset, stdio_origin) == 0) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

static drflac_bool32 flac_tell_cb(void* pUserData, drflac_int64* pCursor)
{
    FILE *f = (FILE*)pUserData;
    long pos = ftell(f);
    if (pos < 0 || !pCursor) return DRFLAC_FALSE;
    *pCursor = (drflac_int64)pos;
    return DRFLAC_TRUE;
}

FlacDecoder::FlacDecoder()
    : m_file(NULL), m_pFlac(NULL), m_current_frame(0), m_total_frames(0)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_FLAC;
}

FlacDecoder::~FlacDecoder()
{
    close();
}

bool FlacDecoder::open(FILE *file)
{
    if (!file) return false;
    m_file = file;
    fseek(m_file, 0, SEEK_SET);

    drflac *pFlac = drflac_open(flac_read_cb, flac_seek_cb, flac_tell_cb, (void*)m_file, NULL);
    if (!pFlac) {
        ESP_LOGE(TAG, "drflac 打开失败，非有效 FLAC 文件");
        return false;
    }
    m_pFlac = (void*)pFlac;

    m_info.fmt.sample_rate = pFlac->sampleRate;
    m_info.fmt.channels = (uint8_t)pFlac->channels;
    m_info.fmt.bits_per_sample = (uint8_t)pFlac->bitsPerSample;
    m_total_frames = pFlac->totalPCMFrameCount;
    m_info.total_time_sec = m_info.fmt.sample_rate ? (uint32_t)(m_total_frames / m_info.fmt.sample_rate) : 0;
    m_current_frame = 0;

    ESP_LOGI(TAG, "成功打开 FLAC: %lu Hz, %d bit, %d 声道, 总帧数: %llu, 时长: %lu 秒",
             (unsigned long)m_info.fmt.sample_rate, m_info.fmt.bits_per_sample,
             m_info.fmt.channels, (unsigned long long)m_total_frames,
             (unsigned long)m_info.total_time_sec);

    return true;
}

const audio_track_info_t* FlacDecoder::get_info() const
{
    return &m_info;
}

bool FlacDecoder::decode_frame(void *pcm_out, size_t max_bytes, size_t *out_bytes)
{
    drflac *pFlac = (drflac*)m_pFlac;
    if (!pFlac || m_current_frame >= m_total_frames) {
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    uint32_t channels = pFlac->channels;
    uint32_t bps = pFlac->bitsPerSample;
    size_t bytes_per_frame = channels * (bps <= 16 ? 2 : 4);
    size_t max_frames = max_bytes / bytes_per_frame;

    uint64_t frames_read = 0;
    if (bps <= 16) {
        frames_read = drflac_read_pcm_frames_s16(pFlac, max_frames, (drflac_int16*)pcm_out);
    } else {
        frames_read = drflac_read_pcm_frames_s32(pFlac, max_frames, (drflac_int32*)pcm_out);
    }

    m_current_frame += frames_read;
    if (out_bytes) {
        *out_bytes = (size_t)(frames_read * bytes_per_frame);
    }

    if (m_info.fmt.sample_rate) {
        m_info.current_time_sec = (uint32_t)(m_current_frame / m_info.fmt.sample_rate);
    }

    return (frames_read > 0);
}

bool FlacDecoder::seek(uint32_t target_sec)
{
    drflac *pFlac = (drflac*)m_pFlac;
    if (!pFlac || !m_info.fmt.sample_rate) return false;

    uint64_t target_frame = (uint64_t)target_sec * m_info.fmt.sample_rate;
    if (target_frame > m_total_frames) target_frame = m_total_frames;

    if (drflac_seek_to_pcm_frame(pFlac, target_frame)) {
        m_current_frame = target_frame;
        m_info.current_time_sec = target_sec;
        return true;
    }
    return false;
}

void FlacDecoder::close()
{
    if (m_pFlac) {
        drflac_close((drflac*)m_pFlac);
        m_pFlac = NULL;
    }
    m_file = NULL;
    m_current_frame = 0;
}
