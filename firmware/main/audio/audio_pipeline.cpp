/**
 * FAKE_POD_NANO 音频输出管线实现 (I2S DMA + CS43131)
 */

#include "audio_pipeline.h"
#include "board.h"
#include "board_config.h"
#include "dac_cs43131.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "AUDIO_PIPE";

static i2s_chan_handle_t s_tx_chan = NULL;
static SemaphoreHandle_t s_lock = NULL;
static audio_owner_t s_owner = AUDIO_OWNER_NONE;
static audio_format_t s_current_fmt = {
    .sample_rate = 44100,       /* 硬件固定 44.1k（本板唯一确认可出声的配置） */
    .bits_per_sample = 16,
    .channels = 2,
};
static uint8_t s_current_volume = 80;
static bool s_is_muted = false;

/* ---------------- 统一输出格式（任意 PCM → 44.1k/16bit/立体声） ---------------- */
#define OUT_RATE_FIXED     44100
#define OUT_CHANNELS_FIXED 2

static audio_format_t s_src_fmt = {
    .sample_rate = OUT_RATE_FIXED,
    .bits_per_sample = 16,
    .channels = OUT_CHANNELS_FIXED,
};
static bool   s_conv_enabled = false;
static double s_src_pos = 0.0;          /* 重采样相位（可跨块为负） */
static int32_t s_prev[2] = { 0, 0 };    /* 上一块最后一帧，用于跨块插值 */

/* 前置声明：均衡器系数重算（reconfig 中调用，定义在文件后部） */
static void eq_recompute(void);
typedef enum {
    PIPE_IDLE = 0,
    PIPE_HELD,
    PIPE_QUIESCE,
    PIPE_APPLY,
} pipe_phase_t;

static pipe_phase_t s_phase = PIPE_IDLE;
static int s_writers = 0;
static portMUX_TYPE s_phase_mux = portMUX_INITIALIZER_UNLOCKED;

static void phase_set(pipe_phase_t phase)
{
    portENTER_CRITICAL(&s_phase_mux);
    s_phase = phase;
    portEXIT_CRITICAL(&s_phase_mux);
}

static bool phase_is(pipe_phase_t phase)
{
    portENTER_CRITICAL(&s_phase_mux);
    bool same = s_phase == phase;
    portEXIT_CRITICAL(&s_phase_mux);
    return same;
}

static bool pipeline_lock(void)
{
    if (!s_lock) return false;
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(500)) == pdTRUE;
}

static void pipeline_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* 24-bit PCM 在 ESP-IDF 上必须放进 32-bit slot，且样本左对齐。 */
static i2s_std_slot_config_t make_slot(const audio_format_t *fmt)
{
    i2s_data_bit_width_t width = I2S_DATA_BIT_WIDTH_16BIT;
    if (fmt->bits_per_sample == 32 || fmt->bits_per_sample == 24) {
        width = I2S_DATA_BIT_WIDTH_32BIT;
    } else if (fmt->bits_per_sample == 8) {
        width = I2S_DATA_BIT_WIDTH_8BIT;
    }
    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        width,
        fmt->channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
    );
    if (fmt->bits_per_sample == 24 || fmt->bits_per_sample == 32) {
        slot.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    }
    return slot;
}

/* 按 Philips / MSB(左对齐) 重新配置 I2S 槽格式（需先停通道） */
static esp_err_t slot_reformat(bool msb)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    i2s_channel_disable(s_tx_chan);

    i2s_data_bit_width_t width = I2S_DATA_BIT_WIDTH_16BIT;
    if (s_current_fmt.bits_per_sample == 32 || s_current_fmt.bits_per_sample == 24) {
        width = I2S_DATA_BIT_WIDTH_32BIT;
    }
    i2s_slot_mode_t mode = (s_current_fmt.channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    i2s_std_slot_config_t slot;
    if (msb) {
        i2s_std_slot_config_t tmp = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(width, mode);
        slot = tmp;
    } else {
        i2s_std_slot_config_t tmp = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, mode);
        slot = tmp;
    }
    if (s_current_fmt.bits_per_sample == 24 || s_current_fmt.bits_per_sample == 32) {
        slot.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    }
    esp_err_t e = i2s_channel_reconfig_std_slot(s_tx_chan, &slot);
    i2s_channel_enable(s_tx_chan);
    return e;
}

/* ---------------------------------------------------------------
 * 初始化 I2S 硬件通道
 * --------------------------------------------------------------- */
static esp_err_t i2s_driver_setup(const audio_format_t *fmt)
{
    /* 1. 通道分配（启用 TX 通道，使用 APLL 获得高保真超低抖动音频时钟） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建 I2S TX 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 标准 Philips 模式配置 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = fmt->sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT, /* ESP32-S3 默认高精时钟 */
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = make_slot(fmt),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)AUDIO_I2S_BCK_IO,
            .ws   = (gpio_num_t)AUDIO_I2S_WS_IO,
            .dout = (gpio_num_t)AUDIO_I2S_DO_IO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 标准模式初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动 I2S 通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S 通道已就绪 (BCK=%d, WS=%d, DOUT=%d, %lu Hz, %d bit)",
             AUDIO_I2S_BCK_IO, AUDIO_I2S_WS_IO, AUDIO_I2S_DO_IO,
             (unsigned long)fmt->sample_rate, fmt->bits_per_sample);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 音频管线初始化
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_init(void)
{
    ESP_LOGI(TAG, "初始化音频管线...");

    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "无法获取 I2C 总线，必须先调用 board_init()");
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. 初始化 CS43131 硬件与模拟链路 */
    esp_err_t ret = cs43131_init_v4(i2c_bus, s_current_fmt.sample_rate);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CS43131 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 初始化 ESP32 I2S DMA 主机通道 */
    ret = i2s_driver_setup(&s_current_fmt);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 同步默认音量 */
    cs43131_set_volume(i2c_bus, s_current_volume);

    ESP_LOGI(TAG, "音频管线初始化成功 (CS43131 + I2S DMA 就绪)！");
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 动态调整采样率与位深
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_acquire(audio_owner_t who)
{
    if (who == AUDIO_OWNER_NONE) return ESP_ERR_INVALID_ARG;
    if (!pipeline_lock()) return ESP_ERR_TIMEOUT;
    esp_err_t ret = ESP_OK;
    if ((s_phase != PIPE_IDLE && s_phase != PIPE_HELD) ||
        (s_owner != AUDIO_OWNER_NONE && s_owner != who)) {
        ret = ESP_ERR_INVALID_STATE;
    } else {
        s_owner = who;
        s_phase = PIPE_HELD;
    }
    pipeline_unlock();
    return ret;
}

void audio_pipeline_release(audio_owner_t who)
{
    if (!pipeline_lock()) return;
    if (s_owner == who && (s_phase == PIPE_HELD || s_phase == PIPE_QUIESCE)) {
        s_owner = AUDIO_OWNER_NONE;
        s_phase = PIPE_IDLE;
    }
    pipeline_unlock();
}

audio_owner_t audio_pipeline_owner(void)
{
    return s_owner;
}

esp_err_t audio_pipeline_reconfig(const audio_format_t *fmt)
{
    if (!fmt || !s_tx_chan) return ESP_ERR_INVALID_ARG;
    if (!pipeline_lock()) return ESP_ERR_TIMEOUT;
    if (s_owner == AUDIO_OWNER_NONE) {
        pipeline_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    /* 源格式 → 硬件固定输出 44.1k/16bit/立体声；不一致时启用软件重采样。
     * DoP（176.4k）必须直通，不能重采样。 */
    audio_format_t src = *fmt;
    bool need_conv = !(src.sample_rate == OUT_RATE_FIXED &&
                       src.bits_per_sample == 16 &&
                       src.channels == OUT_CHANNELS_FIXED);
    if (src.sample_rate == 176400) need_conv = false;

    audio_format_t hw = src;
    if (need_conv) {
        hw.sample_rate = OUT_RATE_FIXED;
        hw.bits_per_sample = 16;
        hw.channels = OUT_CHANNELS_FIXED;
    }

    if (s_src_fmt.sample_rate == src.sample_rate &&
        s_src_fmt.bits_per_sample == src.bits_per_sample &&
        s_src_fmt.channels == src.channels &&
        s_conv_enabled == need_conv &&
        s_current_fmt.sample_rate == hw.sample_rate &&
        s_current_fmt.bits_per_sample == hw.bits_per_sample &&
        s_current_fmt.channels == hw.channels) {
        pipeline_unlock();
        return ESP_OK;
    }

    s_src_fmt = src;
    s_conv_enabled = need_conv;
    s_src_pos = 0.0;
    s_prev[0] = 0;
    s_prev[1] = 0;

    fmt = &hw;   /* 之后一律按硬件格式配置 I2S/DAC */

    ESP_LOGI(TAG, "重配音频输出: 源 %lu Hz/%d bit/%dch → 输出 %lu Hz/%d bit/%dch（%s）",
             (unsigned long)src.sample_rate, src.bits_per_sample, src.channels,
             (unsigned long)hw.sample_rate, hw.bits_per_sample, hw.channels,
             need_conv ? "软件重采样" : "直通");

    phase_set(PIPE_QUIESCE);

    int spins = 0;
    int writers = 1;
    while (writers > 0 && spins < 300) {
        portENTER_CRITICAL(&s_phase_mux);
        writers = s_writers;
        portEXIT_CRITICAL(&s_phase_mux);
        if (writers > 0) {
            pipeline_unlock();
            vTaskDelay(1);
            spins++;
            if (!pipeline_lock()) return ESP_ERR_TIMEOUT;
            if (s_phase == PIPE_IDLE) {
                pipeline_unlock();
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
    if (writers > 0) {
        phase_set(PIPE_HELD);
        pipeline_unlock();
        return ESP_ERR_TIMEOUT;
    }
    phase_set(PIPE_APPLY);

    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (i2c_bus) {
        cs43131_set_sample_rate(i2c_bus, fmt->sample_rate);
        cs43131_set_sample_bits(i2c_bus, fmt->bits_per_sample);
    }

    esp_err_t ret = i2s_channel_disable(s_tx_chan);
    if (ret != ESP_OK) {
        phase_set(PIPE_HELD);
        pipeline_unlock();
        return ret;
    }

    i2s_std_clk_config_t clk_cfg = {
        .sample_rate_hz = fmt->sample_rate,
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .ext_clk_freq_hz = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };
    ret = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (ret == ESP_OK) {
        i2s_std_slot_config_t slot_cfg = make_slot(fmt);
        ret = i2s_channel_reconfig_std_slot(s_tx_chan, &slot_cfg);
    }
    esp_err_t en = i2s_channel_enable(s_tx_chan);
    if (ret == ESP_OK) ret = en;
    if (ret == ESP_OK) { s_current_fmt = *fmt; eq_recompute(); }
    phase_set(PIPE_HELD);
    pipeline_unlock();
    return ret;
}

/* ---------------------------------------------------------------
 * 3 段 peaking 均衡器（自带 DSP，作用于 PCM 数据）
 * --------------------------------------------------------------- */
typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[2], z2[2];   /* 两个声道状态 */
} eq_band_t;

static eq_band_t s_eq[3];
static int s_eq_preset = 0;          /* 0=关闭 */
static uint8_t *s_eq_buf = NULL;
static size_t s_eq_cap = 0;

/* 预设增益(dB)：低频120Hz / 中频1kHz / 高频8kHz */
static const float kEqGain[5][3] = {
    { 0.0f,  0.0f,  0.0f },   /* 关闭 */
    { 3.0f,  0.0f,  2.5f },   /* 流行 */
    { 4.5f, -1.0f,  3.5f },   /* 摇滚 */
    { 1.0f,  1.0f,  1.0f },   /* 古典 */
    {-2.0f,  4.0f, -1.0f },   /* 人声 */
};

static void eq_set_band(eq_band_t *b, float f0, float gain_db, float q, uint32_t fs)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / (float)fs;
    float alpha = sinf(w0) / (2.0f * q);
    float cosw = cosf(w0);
    float a0 = 1.0f + alpha / A;
    b->b0 = (1.0f + alpha * A) / a0;
    b->b1 = (-2.0f * cosw) / a0;
    b->b2 = (1.0f - alpha * A) / a0;
    b->a1 = (-2.0f * cosw) / a0;
    b->a2 = (1.0f - alpha / A) / a0;
    b->z1[0] = b->z1[1] = 0.0f;
    b->z2[0] = b->z2[1] = 0.0f;
}

static void eq_recompute(void)
{
    uint32_t fs = s_current_fmt.sample_rate ? s_current_fmt.sample_rate : 48000;
    int p = (s_eq_preset < 0) ? 0 : (s_eq_preset > 4 ? 4 : s_eq_preset);
    const float *g = kEqGain[p];
    eq_set_band(&s_eq[0], 120.0f,  g[0], 1.0f, fs);
    eq_set_band(&s_eq[1], 1000.0f, g[1], 1.0f, fs);
    eq_set_band(&s_eq[2], 8000.0f, g[2], 1.0f, fs);
}

esp_err_t audio_pipeline_set_eq(int preset)
{
    if (preset < 0) preset = 0;
    if (preset > 4) preset = 4;
    s_eq_preset = preset;
    eq_recompute();
    ESP_LOGI(TAG, "均衡器预设 -> %d (0关/1流行/2摇滚/3古典/4人声)", preset);
    return ESP_OK;
}

int audio_pipeline_get_eq(void)
{
    return s_eq_preset;
}

/* 对一块 PCM 做 EQ（原地处理到 scratch 缓冲） */
static const void *eq_process(const void *data, size_t size_bytes, size_t *out_len)
{
    *out_len = size_bytes;
    if (s_eq_preset == 0 || size_bytes == 0) return data;
    /* DoP(DSD over PCM) 流必须原样透传，任何 DSP 都会破坏 0x05/0xFA 标记 */
    if (s_current_fmt.sample_rate == 176400) return data;

    int bps = (s_current_fmt.bits_per_sample == 16) ? 2 : 4;
    size_t frames = size_bytes / (size_t)bps;

    if (size_bytes > s_eq_cap) {
        uint8_t *nb = (uint8_t *)realloc(s_eq_buf, size_bytes);
        if (!nb) return data;
        s_eq_buf = nb;
        s_eq_cap = size_bytes;
    }
    memcpy(s_eq_buf, data, size_bytes);

    int ch = (s_current_fmt.channels == 1) ? 1 : 2;
    for (size_t i = 0; i < frames; i++) {
        int c = (int)(i % (size_t)ch);
        float x;
        if (bps == 2) {
            x = (float)((int16_t *)s_eq_buf)[i] / 32768.0f;
        } else {
            x = (float)((int32_t *)s_eq_buf)[i] / 2147483648.0f;
        }
        for (int b = 0; b < 3; b++) {
            eq_band_t *e = &s_eq[b];
            float y = e->b0 * x + e->z1[c];
            e->z1[c] = e->b1 * x - e->a1 * y + e->z2[c];
            e->z2[c] = e->b2 * x - e->a2 * y;
            x = y;
        }
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        if (bps == 2) {
            ((int16_t *)s_eq_buf)[i] = (int16_t)(x * 32767.0f);
        } else {
            ((int32_t *)s_eq_buf)[i] = (int32_t)(x * 2147483647.0f);
        }
    }
    return s_eq_buf;
}

/* ---------------------------------------------------------------
 * 统一输出格式：任意 PCM → 44.1kHz / 16bit / 立体声
 * 原因：本板 CS43131 只在 44.1k/16bit（原厂 ASP 参数）下确认可出声，
 *       其他采样率/位深由软件重采样后统一输出。
 * --------------------------------------------------------------- */
static inline int32_t src_sample_at(const void *base, int32_t idx, int ch, const audio_format_t *f)
{
    if (idx < 0) return s_prev[ch < 0 ? 0 : (ch > 1 ? 1 : ch)];
    int c = (f->channels == 1) ? 0 : (ch > 1 ? 1 : ch);
    if (f->bits_per_sample == 16) {
        const int16_t *p = (const int16_t *)base;
        return p[(size_t)idx * f->channels + c];
    }
    const int32_t *p = (const int32_t *)base;
    int32_t v = p[(size_t)idx * f->channels + c];
    if (f->bits_per_sample == 24) return v >> 8;    /* 24in32 左对齐 */
    return v >> 16;
}

/* 返回写入 out 的 int16 样本个数（立体声交错） */
static size_t conv_to_fixed(const void *data, size_t in_bytes, int16_t *out, size_t out_cap_samples)
{
    const audio_format_t *f = &s_src_fmt;
    size_t bytes_per_sample = (f->bits_per_sample == 16) ? 2 : 4;
    size_t bpf = bytes_per_sample * f->channels;
    if (!bpf) return 0;
    uint32_t n = (uint32_t)(in_bytes / bpf);
    if (n == 0) return 0;

    double ratio = (double)f->sample_rate / (double)OUT_RATE_FIXED;
    size_t o = 0;
    while (o + 2 <= out_cap_samples) {
        int32_t i0 = (int32_t)floor(s_src_pos);
        if (i0 + 1 >= (int32_t)n) break;
        double frac = s_src_pos - (double)i0;
        for (int ch = 0; ch < OUT_CHANNELS_FIXED; ch++) {
            int32_t s0 = src_sample_at(data, i0, ch, f);
            int32_t s1 = src_sample_at(data, i0 + 1, ch, f);
            double v = (double)s0 + ((double)s1 - (double)s0) * frac;
            if (v > 32767.0) v = 32767.0;
            else if (v < -32768.0) v = -32768.0;
            out[o + (size_t)ch] = (int16_t)v;
        }
        o += OUT_CHANNELS_FIXED;
        s_src_pos += ratio;
    }
    /* 跨块保留相位与最后一帧 */
    s_src_pos -= (double)n;
    if (n > 0) {
        s_prev[0] = src_sample_at(data, (int32_t)n - 1, 0, f);
        s_prev[1] = src_sample_at(data, (int32_t)n - 1, 1, f);
    }
    return o;
}

/* ---------------------------------------------------------------
 * PCM 数据流写入
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_write(const void *data, size_t size_bytes, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!s_tx_chan || !data) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_phase_mux);
    if (s_phase != PIPE_HELD) {
        portEXIT_CRITICAL(&s_phase_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_writers++;
    portEXIT_CRITICAL(&s_phase_mux);

    int16_t *conv_buf = NULL;
    const void *out = data;
    size_t out_len = size_bytes;

    if (s_conv_enabled) {
        size_t bpf = (((s_src_fmt.bits_per_sample == 16) ? 2 : 4) * s_src_fmt.channels);
        size_t in_frames = bpf ? (size_bytes / bpf) : 0;
        double ratio = (double)s_src_fmt.sample_rate / (double)OUT_RATE_FIXED;
        size_t out_frames = (size_t)(ratio > 0.0 ? ((double)in_frames / ratio) : 0.0) + 16;
        conv_buf = (int16_t *)malloc(out_frames * OUT_CHANNELS_FIXED * sizeof(int16_t));
        if (conv_buf) {
            size_t got = conv_to_fixed(data, size_bytes, conv_buf,
                                       out_frames * OUT_CHANNELS_FIXED);
            out = conv_buf;
            out_len = got * sizeof(int16_t);
        }
    }

    if (out_len == 0) {
        free(conv_buf);
        portENTER_CRITICAL(&s_phase_mux);
        s_writers--;
        portEXIT_CRITICAL(&s_phase_mux);
        if (bytes_written) *bytes_written = size_bytes;
        return ESP_OK;
    }

    size_t eq_len = out_len;
    const void *eq_out = eq_process(out, out_len, &eq_len);

    esp_err_t ret = i2s_channel_write(s_tx_chan, eq_out, eq_len, bytes_written, pdMS_TO_TICKS(timeout_ms));

    free(conv_buf);
    portENTER_CRITICAL(&s_phase_mux);
    s_writers--;
    portEXIT_CRITICAL(&s_phase_mux);
    return ret;
}

esp_err_t audio_pipeline_flush(void)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    /* auto_clear 会在停止写入后输出静音；这里不再二次 disable，避免和正在进行的 write 交叉。 */
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 音量控制
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_current_volume = volume;
    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (i2c_bus) {
        cs43131_set_volume(i2c_bus, volume);
    }
    return ESP_OK;
}

uint8_t audio_pipeline_get_volume(void)
{
    return s_current_volume;
}

esp_err_t audio_pipeline_set_mute(bool mute)
{
    s_is_muted = mute;
    i2c_master_bus_handle_t i2c_bus = board_i2c_bus_get();
    if (i2c_bus) {
        cs43131_set_mute(i2c_bus, mute);
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 测试音：直接经 I2S -> CS43131 -> 耳放 输出，用于排查 DAC 通路
 * --------------------------------------------------------------- */
esp_err_t audio_pipeline_play_test_tone(uint32_t freq_hz, uint32_t ms, uint8_t volume_pct)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;

    if (audio_pipeline_acquire(AUDIO_OWNER_PLAYER) != ESP_OK) {
        ESP_LOGW(TAG, "测试音：输出被占用（USB 声卡？），请先退出 DAC 页");
        return ESP_ERR_INVALID_STATE;
    }

    audio_pipeline_set_mute(false);
    audio_pipeline_set_volume(volume_pct);

    /* 硬件固定 44.1k/16bit：先做一次完整初始化（复位/晶振/耳放/音量/静音），
     * 再输出 1kHz 正弦。音质档位不再扫描——任何源格式都由 write 层重采样。 */
    i2c_master_bus_handle_t bus = board_i2c_bus_get();
    if (bus) {
        cs43131_init_v4(bus, OUT_RATE_FIXED);
    }

    audio_format_t fmt = {
        .sample_rate = OUT_RATE_FIXED,
        .bits_per_sample = 16,
        .channels = OUT_CHANNELS_FIXED
    };
    audio_pipeline_reconfig(&fmt);

    {
        int bits = 16;
        const int frames = 512;
        size_t bytes_per = sizeof(int16_t);
        void *buf = malloc((size_t)frames * 2 * bytes_per);
        if (!buf) {
            audio_pipeline_release(AUDIO_OWNER_PLAYER);
            return ESP_ERR_NO_MEM;
        }

        uint32_t sr = OUT_RATE_FIXED;
        uint32_t total = (sr * ms) / 1000u;
        double phase = 0.0;
        double step = 2.0 * M_PI * (double)freq_hz / (double)sr;
        uint32_t done = 0;
        while (done < total) {
            for (int i = 0; i < frames; i++) {
                double s = sin(phase) * 12000.0;
                ((int16_t *)buf)[i * 2] = (int16_t)s;
                ((int16_t *)buf)[i * 2 + 1] = (int16_t)s;
                phase += step;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            }
            size_t written = 0;
            audio_pipeline_write(buf, (size_t)frames * 2 * bytes_per, &written, 100);
            done += (uint32_t)frames;
        }
        free(buf);
    }

    audio_pipeline_release(AUDIO_OWNER_PLAYER);
    ESP_LOGI(TAG, "组合轮播结束：哪一组是干净正弦音请反馈组号 1~4");
    return ESP_OK;
}
