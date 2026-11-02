/**
 * FAKE_POD_NANO AAC / M4A 音频解码器实现
 * ============================================
 * - .aac (ADTS 帧流)：直接交给 Helix AAC 解码
 * - .m4a (MP4 容器)：解析 moov/trak/mdia/minf/stbl 取样本表，
 *   再按样本逐个交给 Helix（raw block 模式）解码
 */

#include "decoder_aac.h"
#include "aacdec.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "AAC_DEC";

#define AAC_IN_BUF_SIZE (4096)

/* ---------------- 大端辅助 ---------------- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/* 在 [buf,len) 中查找指定类型 box，返回其 payload 指针，长度写入 *out_len */
static const uint8_t *find_box(const uint8_t *buf, size_t len, const char *type, size_t *out_len)
{
    size_t off = 0;
    while (off + 8 <= len) {
        uint32_t sz = be32(buf + off);
        if (sz < 8) break;
        if (memcmp(buf + off + 4, type, 4) == 0) {
            if (out_len) *out_len = (size_t)(sz - 8);
            return buf + off + 8;
        }
        if (off + sz > len) break;
        off += sz;
    }
    return NULL;
}

/* ---------------- esds -> AudioSpecificConfig ---------------- */
static int desc_read_len(const uint8_t *p, size_t avail, size_t *consumed)
{
    uint32_t len = 0;
    size_t i = 0;
    while (i < 4 && i < avail) {
        uint8_t b = p[i];
        len = (len << 7) | (b & 0x7F);
        i++;
        if (!(b & 0x80)) break;
    }
    *consumed = i;
    return (int)len;
}

static bool esds_get_asc(const uint8_t *d, size_t len, uint8_t *asc, size_t *asc_len)
{
    if (!d || len < 5) return false;
    size_t off = 4;  /* version + flags */
    if (off >= len || d[off] != 0x03) return false;
    off++;
    size_t c = 0;
    desc_read_len(d + off, len - off, &c);
    off += c;
    if (off + 3 > len) return false;
    off += 2;                 /* ES_ID */
    uint8_t flags = d[off++];
    if (flags & 0x80) off += 2;
    if (flags & 0x40) { if (off < len) off += 1; }
    if (flags & 0x20) off += 2;
    if (off >= len || d[off] != 0x04) return false;
    off++;
    desc_read_len(d + off, len - off, &c);
    off += c;
    off += 13;                /* objType+streamType+bufferSize(3)+maxBit(4)+avgBit(4) */
    if (off >= len || d[off] != 0x05) return false;
    off++;
    int dl = desc_read_len(d + off, len - off, &c);
    off += c;
    if (dl <= 0 || off + (size_t)dl > len) return false;
    if ((size_t)dl > 32) dl = 32;
    memcpy(asc, d + off, (size_t)dl);
    *asc_len = (size_t)dl;
    return true;
}

static void asc_params(const uint8_t *asc, size_t n, int *rate, int *ch)
{
    static const int rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                   22050, 16000, 12000, 11025, 8000, 7350 };
    *rate = 44100;
    *ch = 2;
    if (n < 2) return;
    int freq = ((asc[0] & 0x07) << 1) | ((asc[1] >> 7) & 0x01);
    int chans = (asc[1] >> 3) & 0x0F;
    if (freq >= 0 && freq < 13) *rate = rates[freq];
    if (chans > 0 && chans <= 8) *ch = chans;
}

AacDecoder::AacDecoder()
    : m_file(NULL), m_hAACDecoder(NULL), m_in_buf(NULL), m_pcm(NULL), m_in_buf_size(AAC_IN_BUF_SIZE),
      m_in_buf_len(0), m_data_offset(0), m_file_size(0), m_total_samples(0), m_current_samples(0),
      m_is_mp4(false), m_off(NULL), m_size(NULL), m_count(0), m_idx(0), m_timescale(0)
{
    memset(&m_info, 0, sizeof(m_info));
    m_info.type = DECODER_TYPE_AAC_M4A;
}

AacDecoder::~AacDecoder()
{
    close();
}

/* ---------------------------------------------------------------
 * M4A 解封装：读出所有 AAC sample 的 (偏移, 大小)
 * --------------------------------------------------------------- */
bool AacDecoder::parse_mp4(FILE *f)
{
    /* 1. 读出整个 moov box（可能在前或在后） */
    fseek(f, 0, SEEK_SET);
    long moov_off = -1;
    uint32_t moov_len = 0;
    while (true) {
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) break;
        uint32_t sz = be32(h);
        if (memcmp(h + 4, "moov", 4) == 0) {
            moov_off = ftell(f);
            moov_len = (sz >= 8) ? sz - 8 : 0;
            break;
        }
        if (sz < 8) break;
        if (fseek(f, (long)sz - 8, SEEK_CUR) != 0) break;
    }
    if (moov_off < 0 || moov_len < 64 || moov_len > (24u << 20)) {
        ESP_LOGE(TAG, "未找到有效 moov (off=%ld len=%lu)", moov_off, (unsigned long)moov_len);
        return false;
    }

    uint8_t *moov = (uint8_t *)malloc(moov_len);
    if (!moov) return false;
    fseek(f, moov_off, SEEK_SET);
    if (fread(moov, 1, moov_len, f) != moov_len) { free(moov); return false; }

    /* 2. 找带 soun handler 的 trak */
    const size_t M = moov_len;
    const uint8_t *stbl = NULL;
    size_t stbl_len = 0;
    size_t off = 0;
    while (off + 8 <= M) {
        uint32_t sz = be32(moov + off);
        if (sz < 8 || off + sz > M) break;
        if (memcmp(moov + off + 4, "trak", 4) == 0) {
            const uint8_t *trak = moov + off + 8;
            size_t trak_len = sz - 8;
            size_t mdia_len = 0;
            const uint8_t *mdia = find_box(trak, trak_len, "mdia", &mdia_len);
            if (mdia) {
                size_t hdlr_len = 0;
                const uint8_t *hdlr = find_box(mdia, mdia_len, "hdlr", &hdlr_len);
                bool is_soun = hdlr && hdlr_len >= 12 && memcmp(hdlr + 8, "soun", 4) == 0;
                if (is_soun) {
                    /* mdhd timescale */
                    size_t mdhd_len = 0;
                    const uint8_t *mdhd = find_box(mdia, mdia_len, "mdhd", &mdhd_len);
                    if (mdhd && mdhd_len >= 20) {
                        uint8_t ver = mdhd[0];
                        m_timescale = (ver == 1) ? be32(mdhd + 20) : be32(mdhd + 12);
                    }
                    size_t minf_len = 0;
                    const uint8_t *minf = find_box(mdia, mdia_len, "minf", &minf_len);
                    if (minf) {
                        stbl = find_box(minf, minf_len, "stbl", &stbl_len);
                    }
                    if (stbl) break;
                }
            }
        }
        off += sz;
    }
    if (!stbl) { ESP_LOGE(TAG, "未找到音频 stbl"); free(moov); return false; }

    /* 3. stsd：取通道数/采样率 + esds ASC */
    size_t stsd_len = 0;
    const uint8_t *stsd = find_box(stbl, stbl_len, "stsd", &stsd_len);
    int rate = 44100, chans = 2;
    uint8_t asc[32];
    size_t asc_len = 0;
    if (stsd && stsd_len > 16) {
        const uint8_t *entry = stsd + 8;   /* version/flags + entry_count */
        uint32_t esz = be32(entry);
        if (esz >= 36) {
            uint16_t chan = be16(entry + 24);
            uint32_t srate = be32(entry + 32) >> 16;   /* 16.16 */
            if (chan) chans = chan;
            if (srate) rate = (int)srate;
            size_t csz = 0;
            const uint8_t *esds = find_box(entry + 36, esz - 36, "esds", &csz);
            if (esds) {
                if (!esds_get_asc(esds, csz, asc, &asc_len)) {
                    ESP_LOGW(TAG, "esds 中未解析出 ASC，将用 stsd 的采样率/声道");
                }
            }
        }
    }
    if (asc_len) asc_params(asc, asc_len, &rate, &chans);

    /* 4. stsz 样本大小 */
    size_t stsz_len = 0;
    const uint8_t *stsz = find_box(stbl, stbl_len, "stsz", &stsz_len);
    if (!stsz || stsz_len < 12) { ESP_LOGE(TAG, "缺少 stsz"); free(moov); return false; }
    uint32_t fixed_size = be32(stsz + 4);
    uint32_t count = be32(stsz + 8);
    if (count == 0 || count > 2000000) { ESP_LOGE(TAG, "stsz 样本数异常: %lu", (unsigned long)count); free(moov); return false; }

    /* 5. stco / co64 块偏移 */
    size_t stco_len = 0;
    const uint8_t *stco = find_box(stbl, stbl_len, "stco", &stco_len);
    const uint8_t *co64 = NULL;
    size_t co64_len = 0;
    if (!stco) co64 = find_box(stbl, stbl_len, "co64", &co64_len);
    const uint8_t *cho = stco ? stco : co64;
    if (!cho) { ESP_LOGE(TAG, "缺少 stco/co64"); free(moov); return false; }
    uint32_t chunk_count = be32(cho + 4);

    /* 6. stsc 样本到块的映射 */
    size_t stsc_len = 0;
    const uint8_t *stsc = find_box(stbl, stbl_len, "stsc", &stsc_len);
    if (!stsc || stsc_len < 8) { ESP_LOGE(TAG, "缺少 stsc"); free(moov); return false; }
    uint32_t stsc_count = be32(stsc + 4);
    if (stsc_count == 0 || 8 + stsc_count * 12 > stsc_len) { free(moov); return false; }

    /* 7. 分配并填充样本表 */
    m_off = (uint64_t *)malloc((size_t)count * sizeof(uint64_t));
    m_size = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
    if (!m_off || !m_size) { ESP_LOGE(TAG, "样本表内存不足"); free(moov); return false; }

    uint32_t si = 0;             /* 已填充样本序号 */
    uint32_t entry_idx = 0;      /* 当前 stsc 条目 */
    for (uint32_t ci = 0; ci < chunk_count && si < count; ci++) {
        /* 找到覆盖该块(1-based)的 stsc 条目 */
        while (entry_idx + 1 < stsc_count) {
            uint32_t next_first = be32(stsc + 8 + (entry_idx + 1) * 12);
            if ((ci + 1) >= next_first) entry_idx++;
            else break;
        }
        uint32_t spc = be32(stsc + 8 + entry_idx * 12 + 4);
        if (spc == 0) spc = 1;
        uint64_t pos;
        if (stco) pos = (uint64_t)be32(cho + 8 + ci * 4);
        else {
            const uint8_t *p = cho + 8 + ci * 8;
            pos = ((uint64_t)be32(p) << 32) | be32(p + 4);
        }
        for (uint32_t j = 0; j < spc && si < count; j++) {
            uint32_t sz = fixed_size ? fixed_size : be32(stsz + 12 + si * 4);
            m_off[si] = pos;
            m_size[si] = sz;
            pos += sz;
            si++;
        }
    }
    m_count = si;
    m_idx = 0;

    /* 8. 总时长：stts 求和 / timescale */
    uint32_t total_units = 0;
    size_t stts_len = 0;
    const uint8_t *stts = find_box(stbl, stbl_len, "stts", &stts_len);
    if (stts && stts_len >= 8) {
        uint32_t n = be32(stts + 4);
        for (uint32_t i = 0; i < n && 8 + i * 8 + 8 <= stts_len; i++) {
            uint32_t sc = be32(stts + 8 + i * 8);
            uint32_t sd = be32(stts + 8 + i * 8 + 4);
            total_units += sc * sd;
        }
    }
    if (m_timescale == 0) m_timescale = (uint32_t)rate;
    uint32_t dur_sec = (m_timescale && total_units) ? (total_units / m_timescale) : 0;

    m_info.fmt.sample_rate = (uint32_t)rate;
    m_info.fmt.channels = (uint8_t)chans;
    m_info.fmt.bits_per_sample = 16;
    m_info.total_time_sec = dur_sec;
    if (dur_sec && m_count) m_info.bit_rate = (uint32_t)((uint64_t)m_file_size * 8 / (dur_sec * 1000));

    ESP_LOGI(TAG, "M4A 解析成功: %d Hz, %d 声道, %lu 个 AAC 帧, 时长 %lu s",
             rate, chans, (unsigned long)m_count, (unsigned long)dur_sec);

    free(moov);
    return m_count > 0;
}

/* ADTS 路径：仅定位 mdat（旧逻辑保留用于原生 .aac） */
bool AacDecoder::parse_container()
{
    fseek(m_file, 0, SEEK_SET);
    uint8_t header[8];
    if (fread(header, 1, 8, m_file) != 8) return false;
    if (memcmp(header + 4, "ftyp", 4) == 0) {
        fseek(m_file, 0, SEEK_SET);
    } else {
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

    uint8_t probe[8] = {0};
    size_t pr = fread(probe, 1, 8, m_file);
    fseek(m_file, 0, SEEK_SET);
    m_is_mp4 = (pr == 8 && memcmp(probe + 4, "ftyp", 4) == 0);

    m_hAACDecoder = (void*)AACInitDecoder();
    if (!m_hAACDecoder) {
        ESP_LOGE(TAG, "AACInitDecoder 初始化失败");
        return false;
    }
    m_in_buf = (uint8_t*)malloc(m_in_buf_size);
    m_pcm = (short*)malloc(2048 * 2 * sizeof(short));
    if (!m_in_buf || !m_pcm) { close(); return false; }

    if (m_is_mp4) {
        if (!parse_mp4(m_file)) { close(); return false; }
        AACFrameInfo fi;
        memset(&fi, 0, sizeof(fi));
        fi.nChans = m_info.fmt.channels ? m_info.fmt.channels : 2;
        fi.sampRateCore = m_info.fmt.sample_rate ? m_info.fmt.sample_rate : 44100;
        fi.sampRateOut = fi.sampRateCore;
        fi.profile = 1;   /* LC */
        fi.bitsPerSample = 16;
        int r = AACSetRawBlockParams((HAACDecoder)m_hAACDecoder, 0, &fi);
        if (r != 0) {
            ESP_LOGE(TAG, "AACSetRawBlockParams 失败: %d", r);
            close();
            return false;
        }
        m_idx = 0;
        m_current_samples = 0;
        m_info.type = DECODER_TYPE_AAC_M4A;
        return true;
    }

    /* ---- 原生 ADTS AAC ---- */
    parse_container();
    fseek(m_file, m_data_offset, SEEK_SET);
    m_in_buf_len = fread(m_in_buf, 1, m_in_buf_size, m_file);

    unsigned char *read_ptr = m_in_buf;
    int bytes_left = (int)m_in_buf_len;
    int sync_offset = AACFindSyncWord(read_ptr, bytes_left);
    if (sync_offset < 0) {
        ESP_LOGE(TAG, "未找到 ADTS 同步字");
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

    fseek(m_file, m_data_offset, SEEK_SET);
    m_in_buf_len = 0;
    m_current_samples = 0;
    ESP_LOGI(TAG, "成功打开 ADTS AAC: %lu Hz, %d 声道, %d bit",
             (unsigned long)m_info.fmt.sample_rate, m_info.fmt.channels, m_info.fmt.bits_per_sample);
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

    /* ---- M4A：按样本表逐帧解码 ---- */
    if (m_is_mp4) {
        while (m_idx < m_count) {
            uint32_t sz = m_size[m_idx];
            uint64_t off = m_off[m_idx];
            m_idx++;
            if (sz == 0 || sz > (4u << 20)) continue;
            if (sz > m_in_buf_size) {
                uint8_t *nb = (uint8_t*)realloc(m_in_buf, sz);
                if (!nb) { if (out_bytes) *out_bytes = 0; return false; }
                m_in_buf = nb;
                m_in_buf_size = sz;
            }
            fseek(m_file, (long)off, SEEK_SET);
            if (fread(m_in_buf, 1, sz, m_file) != sz) { if (out_bytes) *out_bytes = 0; return false; }

            unsigned char *p = m_in_buf;
            int left = (int)sz;
            int err = AACDecode((HAACDecoder)m_hAACDecoder, &p, &left, m_pcm);
            if (err != 0) continue;   /* 跳过坏帧 */

            AACFrameInfo fi;
            AACGetLastFrameInfo((HAACDecoder)m_hAACDecoder, &fi);
            size_t pcm_bytes = (size_t)fi.outputSamps * sizeof(short);
            if (pcm_bytes > max_bytes) pcm_bytes = max_bytes;
            memcpy(pcm_out, m_pcm, pcm_bytes);
            if (out_bytes) *out_bytes = pcm_bytes;
            m_current_samples += (uint64_t)(fi.outputSamps / (fi.nChans ? fi.nChans : 1));
            if (m_info.fmt.sample_rate) {
                m_info.current_time_sec = (uint32_t)(m_current_samples / m_info.fmt.sample_rate);
            }
            return true;
        }
        if (out_bytes) *out_bytes = 0;
        return false;
    }

    /* ---- ADTS ---- */
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
    int bytes_left = (int)m_in_buf_len;
    int sync_offset = AACFindSyncWord(read_ptr, bytes_left);
    if (sync_offset < 0) {
        if (feof(m_file)) { if (out_bytes) *out_bytes = 0; return false; }
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

    int consumed = (int)m_in_buf_len - bytes_left;
    if (consumed > 0 && (size_t)consumed <= m_in_buf_len) {
        memmove(m_in_buf, m_in_buf + consumed, m_in_buf_len - consumed);
        m_in_buf_len -= consumed;
    }

    if (err == 0) {
        AACFrameInfo frame_info;
        AACGetLastFrameInfo((HAACDecoder)m_hAACDecoder, &frame_info);
        size_t pcm_bytes = (size_t)frame_info.outputSamps * sizeof(short);
        if (pcm_bytes > max_bytes) pcm_bytes = max_bytes;
        memcpy(pcm_out, m_pcm, pcm_bytes);
        if (out_bytes) *out_bytes = pcm_bytes;
        m_current_samples += (uint64_t)(frame_info.outputSamps / (frame_info.nChans ? frame_info.nChans : 1));
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
    if (!m_file) return false;

    if (m_is_mp4) {
        if (!m_count) return false;
        uint32_t total = m_info.total_time_sec;
        if (total && target_sec > total) target_sec = total;
        m_idx = total ? (uint32_t)((uint64_t)target_sec * m_count / total) : 0;
        if (m_idx >= m_count) m_idx = m_count - 1;
        AACFlushCodec((HAACDecoder)m_hAACDecoder);
        m_info.current_time_sec = target_sec;
        m_current_samples = (uint64_t)target_sec * m_info.fmt.sample_rate;
        return true;
    }

    if (!m_info.total_time_sec) return false;
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
    if (m_in_buf) { free(m_in_buf); m_in_buf = NULL; m_in_buf_size = AAC_IN_BUF_SIZE; }
    if (m_pcm) { free(m_pcm); m_pcm = NULL; }
    if (m_off) { free(m_off); m_off = NULL; }
    if (m_size) { free(m_size); m_size = NULL; }
    m_count = 0;
    m_idx = 0;
    m_is_mp4 = false;
    m_file = NULL;
    m_in_buf_len = 0;
    m_current_samples = 0;
}
