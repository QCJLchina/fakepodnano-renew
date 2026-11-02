/**
 * FAKE_POD_NANO CS43131 初始化（v4，严格依照 AUDIO_CHAIN.md 逆向结果）
 * ============================================
 * 原厂流程（两个阶段）：
 *  阶段一：复位 → 探测 0x30~0x33 → DevId 校验(43/13/10)
 *          → page1/0x04,0x05,0x06 → page2/0x00
 *  阶段二：采样率→分频 a5 → page15/0x00←0x00（两次）
 *          → page15/0x10 ← old & 0xE7（清 bit3+bit4 起振）
 *          → 轮询 INT_STATUS1.bit3 = XTAL_READY
 *          → 下发 16 项 ASP 表 → 写耳放 profile → 等稳定
 *
 * ASP 表（16 项，a5 = 采样率分频档位）：
 *   0x01000B=a5  0x01000C=0x04  0x040010=0x01  0x040011=a5
 *   0x040012=0x08 0x040013=a5   0x040014=0x1F  0x040015=a5
 *   0x040016=0x3F 0x040017=a5   0x040018=0x0C  0x040019=0x0A
 *   0x050000=a5  0x050001=a5    0x05000A=0x07  0x05000B=0x0F
 */

#include "dac_cs43131.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "CS43131V4";

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t w(uint32_t addr, uint8_t val)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[5] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x01,
        val
    };
    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < 3; i++) {
        ret = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
        if (ret == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    ESP_LOGW(TAG, "写 0x%06lX=0x%02X 失败: %s", (unsigned long)addr, val, esp_err_to_name(ret));
    return ret;
}

static esp_err_t r(uint32_t addr, uint8_t *out)
{
    if (!s_dev || !out) return ESP_ERR_INVALID_ARG;
    uint8_t a[4] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x01
    };
    for (int i = 0; i < 3; i++) {
        if (i2c_master_transmit_receive(s_dev, a, sizeof(a), out, 1, 100) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    *out = 0;
    return ESP_FAIL;
}

/* 原厂采样率 → 分频档位 a5 */
static uint8_t rate_div(uint32_t sr)
{
    switch (sr) {
        case 32000:  return 0;
        case 44100:  return 2;
        case 48000:  return 2;
        case 88200:  return 3;
        case 96000:  return 4;
        case 176400: return 5;
        case 192000: return 6;
        default:     return 2;
    }
}

static esp_err_t probe_device(i2c_master_bus_handle_t bus)
{
    for (uint8_t addr = 0x30; addr <= 0x33; addr++) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = addr;
        cfg.scl_speed_hz = 100 * 1000;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) continue;

        uint8_t a[4] = { 0x01, 0x00, 0x00, 0x01 };
        uint8_t id[3] = { 0 };
        if (i2c_master_transmit_receive(dev, a, sizeof(a), id, 3, 100) == ESP_OK &&
            id[0] == 0x43 && id[1] == 0x13 && id[2] == 0x10) {
            s_dev = dev;
            ESP_LOGI(TAG, "在 0x%02X 找到 CS43131（DevId 43 13 10）", addr);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(dev);
    }
    ESP_LOGE(TAG, "0x30~0x33 均无 CS43131 响应");
    return ESP_ERR_NOT_FOUND;
}

static void write_asp_table(uint8_t a5)
{
    w(0x01000B, a5);
    w(0x01000C, 0x04);
    w(0x040010, 0x01);
    w(0x040011, a5);
    w(0x040012, 0x08);
    w(0x040013, a5);
    w(0x040014, 0x1F);
    w(0x040015, a5);
    w(0x040016, 0x3F);
    w(0x040017, a5);
    w(0x040018, 0x0C);
    w(0x040019, 0x0A);
    w(0x050000, a5);
    w(0x050001, a5);
    w(0x05000A, 0x07);
    w(0x05000B, 0x0F);
}

static void write_amp_profile(uint8_t classH, uint8_t hp)
{
    w(0x090000, 0x28);
    w(0x090001, 0x28);
    w(0x090002, 0x28);
    w(0x090003, 0x28);
    w(0x090004, 0xEF);
    w(0x0B0000, 0x20);
    w(0x080000, hp);
    w(0x010010, classH);
    w(0x080032, 0xEF);
}

esp_err_t cs43131_set_word_bits(uint8_t bits)
{
    (void)bits;
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    w(0x01000C, 0x04);
    w(0x05000A, 0x07);
    w(0x05000B, 0x0F);
    return ESP_OK;
}

/* 按采样率/位深配置串口与 ASP（已确认 44.1k/16bit 可出声，作为基准档）：
 *   - SP_SRATE(0x01000B) = 原厂分频档 a5
 *   - SP_BITSIZE(0x01000C)：16bit=0x04，24/32bit=0x01
 *   - 通道位宽：16bit=0x07/0x0F，24bit=0x06/0x0E
 *   - NUM/DEN 与 LRCK 时序跟随原厂表
 */
esp_err_t cs43131_set_stream(uint32_t sample_rate, uint8_t bits)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t a5 = rate_div(sample_rate);

    uint8_t sp  = 0x04;
    uint8_t ch1 = 0x07;
    uint8_t ch2 = 0x0F;
    if (bits > 16) {
        sp  = 0x01;
        ch1 = 0x06;
        ch2 = 0x0E;
    }

    w(0x01000B, a5);
    w(0x01000C, sp);
    w(0x05000A, ch1);
    w(0x05000B, ch2);

    w(0x040010, 0x01); w(0x040011, a5);
    w(0x040012, 0x08); w(0x040013, a5);
    w(0x040014, 0x1F); w(0x040015, a5);
    w(0x040016, 0x3F); w(0x040017, a5);
    w(0x040018, 0x0C); w(0x040019, 0x0A);
    w(0x050000, a5); w(0x050001, a5);

    ESP_LOGI(TAG, "流配置: %lu Hz / %d bit → a5=%d SP_BITSIZE=0x%02X CH=0x%02X/0x%02X",
             (unsigned long)sample_rate, bits, a5, sp, ch1, ch2);
    return ESP_OK;
}

/* 时钟源扫描表：SYS_CLK_CTL_1(0x010006) 候选值
 * bit[1:0] MCLK 源：0=外部晶振 1=PLL 2=RCO；bit2 MCLK_INT：0=24.576M 1=22.579M
 * 写入后立刻回读 DevId 验证 I2C 是否还活着，失联就回退 0x06。 */
static const uint8_t kClkVar[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07 };

int cs43131_clock_count(void)
{
    return (int)(sizeof(kClkVar) / sizeof(kClkVar[0]));
}

esp_err_t cs43131_clock_try(int idx)
{
    if (!s_dev || idx < 0 || idx >= cs43131_clock_count()) return ESP_ERR_INVALID_ARG;
    uint8_t v = kClkVar[idx];
    w(0x010006, v);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t id = 0;
    bool alive = false;
    for (int i = 0; i < 5; i++) {
        if (r(0x010000, &id) == ESP_OK && id == 0x43) { alive = true; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint8_t is1 = 0;
    r(0x0F0000, &is1);
    ESP_LOGI(TAG, "时钟组%d: SYS_CLK=0x%02X I2C=%s IS1=0x%02X",
             idx, v, alive ? "OK" : "失联", is1);
    if (!alive) {
        w(0x010006, 0x06);
        vTaskDelay(pdMS_TO_TICKS(10));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* ASP 变体扫描表：{LOC1, LOC2, CLOCK_CONF, FRAME_CONF, CH1_SZ, CH2_SZ}
 * 组0 = 原厂精确值（基准），其余用于在硬件上枚举 a5 语义/极性/帧长。 */
static const uint8_t kAspVar[][6] = {
    { 0x02, 0x02, 0x0C, 0x0A, 0x07, 0x0F },   /* 0 原厂精确值 */
    { 0x00, 0x00, 0x0C, 0x0A, 0x07, 0x0F },   /* 1 LOC 归零 */
    { 0x00, 0x01, 0x0C, 0x0A, 0x07, 0x0F },   /* 2 LOC 0/1 */
    { 0x02, 0x02, 0x0C, 0x0A, 0x06, 0x0E },   /* 3 通道位宽 24bit */
    { 0x02, 0x02, 0x0C, 0x08, 0x07, 0x0F },   /* 4 帧格式 0x08 */
    { 0x00, 0x00, 0x0C, 0x08, 0x06, 0x0E },   /* 5 LOC0 + 帧0x08 + 24bit */
    { 0x02, 0x02, 0x04, 0x0A, 0x07, 0x0F },   /* 6 时钟极性翻转 */
    { 0x02, 0x02, 0x08, 0x0A, 0x07, 0x0F },   /* 7 时钟极性另一档 */
};

int cs43131_asp_count(void)
{
    return (int)(sizeof(kAspVar) / sizeof(kAspVar[0]));
}

esp_err_t cs43131_asp_try(int idx)
{
    if (!s_dev || idx < 0 || idx >= cs43131_asp_count()) return ESP_ERR_INVALID_ARG;
    w(0x050000, kAspVar[idx][0]);
    w(0x050001, kAspVar[idx][1]);
    w(0x040018, kAspVar[idx][2]);
    w(0x040019, kAspVar[idx][3]);
    w(0x05000A, kAspVar[idx][4]);
    w(0x05000B, kAspVar[idx][5]);
    ESP_LOGI(TAG, "ASP组%d: LOC=%02X/%02X CLK=%02X FRAME=%02X CH=%02X/%02X", idx,
             kAspVar[idx][0], kAspVar[idx][1], kAspVar[idx][2],
             kAspVar[idx][3], kAspVar[idx][4], kAspVar[idx][5]);
    return ESP_OK;
}

esp_err_t cs43131_init_v4(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    uint8_t a5 = rate_div(sample_rate);

    /* 1. 硬件复位 */
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    /* 2. 探测 + DevId 校验 */
    if (probe_device(i2c_bus) != ESP_OK) return ESP_ERR_NOT_FOUND;
    cs43131_set_i2c_dev_handle(s_dev);

    /* 3. 阶段一基础配置 */
    w(0x010004, 0x00);
    w(0x010005, 0x00);
    w(0x010006, 0x06);
    w(0x020000, 0x00);

    /* 4. 上电（清 PDN_XTAL/HP/ASP/PLL） */
    w(0x020000, 0xA0);
    vTaskDelay(pdMS_TO_TICKS(30));

    /* 5. 晶振启动 */
    w(0x0F0000, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    w(0x0F0000, 0x00);

    uint8_t bias = 0;
    if (r(0x0F0010, &bias) == ESP_OK) {
        uint8_t nb = (uint8_t)(bias & 0xE7);
        w(0x0F0010, nb);
        ESP_LOGI(TAG, "晶振偏置: 0x%02X -> 0x%02X", bias, nb);
    }

    /* 6. 等 XTAL_READY：Linux 内核为 INT_STATUS1.bit4(0x10)，
     *    逆向文档写的 bit3 实测不置位，这里 bit4/bit3 任一置位都算就绪 */
    bool ready = false;
    uint8_t st = 0;
    for (int i = 0; i < 40; i++) {
        if (r(0x0F0000, &st) == ESP_OK && (st & 0x18)) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "晶振: INT_STATUS1=0x%02X XTAL_READY=%d", st, ready ? 1 : 0);

    /* 7. 就绪则切 24.576MHz 外部直接时钟 */
    if (ready) {
        w(0x010006, 0x00);
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t chk = 0;
        if (r(0x010000, &chk) != ESP_OK || chk != 0x43) {
            ESP_LOGE(TAG, "切外部时钟后 I2C 失联，回退 RCO");
            w(0x010006, 0x06);
            ready = false;
        }
    } else {
        ESP_LOGW(TAG, "晶振未就绪，保持 RCO");
    }

    /* 8. 原厂 ASP 16 项表 */
    write_asp_table(a5);

    /* 9. 耳放档位（普通耳机） */
    write_amp_profile(0x20, 0x1C);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 10. 取消静音 + 音量 */
    cs43131_set_volume(i2c_bus, 80);
    cs43131_set_mute(i2c_bus, false);

    /* 11. 回读诊断 */
    {
        uint8_t pwr = 0, hp = 0, cls = 0, bits_r = 0, ch1 = 0, ch2 = 0;
        uint8_t is1 = 0, is2 = 0, clk = 0, loc0 = 0;
        r(0x020000, &pwr);
        r(0x080000, &hp);
        r(0x010010, &cls);
        r(0x01000C, &bits_r);
        r(0x05000A, &ch1);
        r(0x05000B, &ch2);
        r(0x0F0000, &is1);
        r(0x0F0001, &is2);
        r(0x010006, &clk);
        r(0x050000, &loc0);
        ESP_LOGI(TAG, "回读: PWR=0x%02X HP=0x%02X classH=0x%02X BITS=0x%02X CH1=0x%02X CH2=0x%02X LOC0=0x%02X",
                 pwr, hp, cls, bits_r, ch1, ch2, loc0);
        ESP_LOGI(TAG, "状态: IS1=0x%02X IS2=0x%02X SYS_CLK=0x%02X a5=%d", is1, is2, clk, a5);
    }

    ESP_LOGI(TAG, "初始化完成（%lu Hz，原厂 ASP 表）", (unsigned long)sample_rate);
    return ESP_OK;
}
