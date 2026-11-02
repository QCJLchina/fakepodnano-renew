/**
 * FAKE_POD_NANO CS43131 完整初始化（v2）
 * ============================================
 * 依据 Linux 内核 cs43130/cs43131 驱动的寄存器定义与默认值重写：
 *   - 0x020000 PWDN_CTL：bit=1 表示断电（PDN_ASP 0x40 / PDN_HP 0x10 ...）
 *   - 0x01000B SP_SRATE / 0x01000C SP_BITSIZE：串口采样率与位宽
 *   - 0x040018 ASP_CLOCK_CONF（bit4=0 → ASP 从模式）/ 0x040019 ASP_FRAME_CONF
 *   - 0x05000A/0x05000B ASP 通道位宽 + 使能（0x04 = CH_EN）
 *   - 0x080000 HP_OUT_CTL_1（bit3 HP_IN_EN）
 *   - 0x0B0000 CLASS_H_CTL
 *   - 0x0D0000 HP_DETECT：置 0 关闭耳机检测，避免未检测到负载而静音
 */

#include "dac_cs43131.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "CS43131V2";

static i2c_master_dev_handle_t s_dev = NULL;

/* bring-up 探测档位：bit0=v4 风格 16bit 帧编码, bit1=打开耳机检测, bit2=外部 24.576M 时钟 */
static int s_probe_profile = 0;
void cs43131_set_probe_profile(int profile) { s_probe_profile = profile; }

static esp_err_t w(uint32_t addr, uint8_t val)
{
    if (!s_dev) {
        ESP_LOGE(TAG, "w(0x%06lX): s_dev 为空！", (unsigned long)addr);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[5] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x00,           /* 原厂写帧第 4 字节 = 0x00（不是 0x01） */
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
        0x00            /* 原厂读帧第 4 字节 = 0x00 */
    };
    for (int i = 0; i < 3; i++) {
        if (i2c_master_transmit_receive(s_dev, a, sizeof(a), out, 1, 100) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    *out = 0;
    return ESP_FAIL;
}

/* 依据采样率写 SP_SRATE（0=32k,1=44.1k,2=48k,3=88.2k,4=96k,5=176.4k,6=192k） */
static uint8_t srate_idx(uint32_t sr)
{
    switch (sr) {
        case 32000:  return 0;
        case 44100:  return 1;
        case 48000:  return 2;
        case 88200:  return 3;
        case 96000:  return 4;
        case 176400: return 5;
        case 192000: return 6;
        default:     return 2;
    }
}

/* SP_BITSIZE：0=32bit 1=24bit 2=16bit 3=8bit */
static uint8_t bitsize_val(uint8_t bits)
{
    switch (bits) {
        case 16: return 0x02;
        case 24: return 0x01;
        case 32: return 0x00;
        default: return 0x02;
    }
}

/* ASP_CH_x_SZ_EN 的通道位宽编码：16bit=0x01 24bit=0x02 32bit=0x03 */
static uint8_t ch_size_val(uint8_t bits)
{
    switch (bits) {
        case 16: return 0x01;
        case 24: return 0x02;
        case 32: return 0x03;
        default: return 0x01;
    }
}

esp_err_t cs43131_init_v2(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate, uint8_t bits)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    if (!s_dev) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = CS43131_I2C_ADDR;
        cfg.scl_speed_hz = 100 * 1000;
        esp_err_t e = i2c_master_bus_add_device(i2c_bus, &cfg, &s_dev);
        if (e != ESP_OK) return e;
    }

    /* 让音量/静音等旧接口复用同一个设备句柄，否则它们会写不进去 */
    cs43131_set_i2c_dev_handle(s_dev);
    ESP_LOGI(TAG, "I2C 设备句柄已建立: %p", (void *)s_dev);

    /* 1. 硬件复位：拉低 15ms，释放后等 60ms */
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    /* 2. 读芯片 ID（DEVID_AB 位于 0x010000） */
    uint8_t id_ab = 0, id_cd = 0, id_e = 0, rev = 0;
    r(0x010000, &id_ab);
    r(0x010001, &id_cd);
    r(0x010002, &id_e);
    r(0x010004, &rev);
    ESP_LOGI(TAG, "芯片 ID: AB=0x%02X CD=0x%02X E=0x%02X REV=0x%02X (CS43131 期望 'C''S'/0x31)", id_ab, id_cd, id_e, rev);

    /* 3. 系统时钟：保持内部 RCO 22.5792MHz。
     *    实测：改成外部 24.576M 直接时钟后，DAC 的 I2C 从机立即失去时钟，
     *    后续所有传输返回 ESP_ERR_INVALID_STATE（总线级失败），说明本板
     *    24.576M 晶振通路不可用，必须留在 RCO。
     */
    w(0x010006, (s_probe_profile & 4) ? 0x00 : 0x06);

    /* 4. 晶振偏置（page2/0x52） */
    w(0x020052, 0x04);

    /* 5. XTAL 就绪轮询（RCO 模式下 XTAL_RDY 亦会置位，仅作观测） */
    {
        bool xtal_ok = false;
        for (int i = 0; i < 20; i++) {
            uint8_t s1 = 0;
            if (r(0x0F0000, &s1) == ESP_OK && (s1 & 0x10)) { xtal_ok = true; break; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        ESP_LOGI(TAG, "时钟状态: XTAL_READY=%d", xtal_ok ? 1 : 0);
    }

    /* 4. 串口采样率与位宽 */
    w(0x01000B, srate_idx(sample_rate));
    w(0x01000C, (s_probe_profile & 1) ? (bits > 16 ? 0x01 : 0x04) : bitsize_val(bits));
    w(0x01000D, 0x03);            /* PAD_INT_CFG 默认 */

    /* 5. 晶振偏置（内部 RCO 时保持默认） */
    /* （已在上方合并到时钟配置） */

    /* 6. ASP 配置：从模式（bit4=0）、I2S 极性；帧格式默认 0x0A */
    w(0x040018, 0x0C);
    w(0x040019, 0x0A);
    w(0x040010, 0x01); w(0x040011, 0x00);   /* ASP_NUM = 1 */
    w(0x040012, 0x08); w(0x040013, 0x00);   /* ASP_DEN = 8 */
    w(0x040014, 0x1F); w(0x040015, 0x00);
    w(0x040016, 0x3F); w(0x040017, 0x00);

    /* 7. ASP 通道位宽 + 使能（0x04 = CH_EN） */
    uint8_t ch = (s_probe_profile & 1) ? (bits > 16 ? 0x02 : 0x03) : ch_size_val(bits);
    w(0x05000A, (uint8_t)(ch | 0x04));
    w(0x05000B, (uint8_t)(ch | 0x04));

    /* 8. PCM 滤波器与通路（先保持静音状态） */
    w(0x090000, 0x02);
    w(0x090003, 0x03);            /* PCM_PATH_CTL_1：MUTE 位 0x03 先置位 */

    /* 9. 耳放与 Class-H */
    w(0x080000, 0x38);            /* HP_OUT_CTL_1: 0x30 | HP_IN_EN(0x08) */
    w(0x0B0000, 0x1E);            /* CLASS_H_CTL 默认 */
    w(0x0D0000, (s_probe_profile & 2) ? 0x01 : 0x00);  /* HP_DETECT：默认关，探测档位2打开 */
    w(0x0E0000, 0x00);            /* HP_LOAD_1 */

    /* 10. 上电：ASP/HP/XTAL/PLL 全部上电（XSP/DSDIF 保持断电） */
    w(0x020000, 0xA0);
    vTaskDelay(pdMS_TO_TICKS(30));

    /* 11. 上电时序（严格对照 Linux cs43130 驱动）
     *   a) DAC 上电（cs43130_dac_event POST_PMU，CS43131 分支）
     *      DXD1=0x99 → 延时 12ms → DXD13(0x080032)=0x00 → DXD1=0
     *   b) 耳放/模拟上电（cs43130_hpin_event PRE_PMU = hpin_postpmu_seq）
     *      DXD1=0x99 → DXD2(0x020019)=1 → DXD14(0x0B0006)=0xDC → DXD15(0x0B0005)=0xE4 → DXD1=0
     */
    w(0x010010, 0x99);
    w(0x080032, 0x20);           /* pop-free 预置 */
    vTaskDelay(pdMS_TO_TICKS(15));
    w(0x080032, 0x00);           /* DXD13：CS43131 上电后必须清 0 */
    w(0x010010, 0x00);

    w(0x010010, 0x99);
    w(0x020019, 0x01);           /* DXD2 耳放上电 */
    w(0x0B0006, 0xDC);           /* DXD14 */
    w(0x0B0005, 0xE4);           /* DXD15 */
    w(0x010010, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 12. 取消静音 */
    w(0x090003, 0x00);
    w(0x090004, 0x00);

    /* 13. 回读诊断 */
    {
        uint8_t pwr = 0, hp = 0, chs = 0, mutes = 0, clk = 0, det = 0, bits_r = 0;
        uint8_t int1 = 0, int2 = 0, int4 = 0, hpst = 0, load1 = 0;
        r(0x020000, &pwr);
        r(0x080000, &hp);
        r(0x05000A, &chs);
        r(0x090003, &mutes);
        r(0x010006, &clk);
        r(0x0D0000, &det);
        r(0x01000C, &bits_r);
        /* 时钟/耳放状态：INT1 高 4 位含 XTAL_RDY/PLL_RDY，HP_STATUS 反映插入与负载 */
        r(0x0F0000, &int1);
        r(0x0F0001, &int2);
        r(0x0F0003, &int4);
        r(0x0D0001, &hpst);
        r(0x0E0000, &load1);
        ESP_LOGI(TAG, "回读: PWR=0x%02X HP=0x%02X CH1=0x%02X MUTE=0x%02X CLK=0x%02X DET=0x%02X BITS=0x%02X",
                 pwr, hp, chs, mutes, clk, det, bits_r);
        ESP_LOGI(TAG, "状态: INT1=0x%02X(XTAL_RDY=%d PLL_RDY=%d) INT2=0x%02X INT4=0x%02X HP_STATUS=0x%02X HP_LOAD1=0x%02X",
                 int1, (int1 & 0x10) ? 1 : 0, (int1 & 0x04) ? 1 : 0, int2, int4, hpst, load1);
    }

    ESP_LOGI(TAG, "初始化完成（%lu Hz / %d bit，ASP 从模式）", (unsigned long)sample_rate, bits);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 原厂初始化序列复刻（从 OTA 反汇编 0x4201bf9c 起逐条提取）
 *   - page3 关闭、ASP 通道宽度、晶振偏置、0F0010 中断掩码、
 *     电源 F6→F2→99、PCM 通路 0x090000/0x090003/0x090001、
 *     class-H 0x0B0000=0x1E、耳放 0x080000=0x30、0x01000D=0x02、
 *     pop-free 0x080032、时钟源 0x010006=0x04（外部晶振 22.5792M）
 * --------------------------------------------------------------- */
/* 轮询中断状态寄存器 0x0F0000 的指定位（bit4=XTAL_RDY, bit2=PLL_RDY） */
static bool vendor_wait_ready(uint8_t mask, int timeout_ms)
{
    TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(timeout_ms)) {
        uint8_t s = 0;
        if (r(0x0F0000, &s) == ESP_OK && (s & mask) == mask) return true;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

esp_err_t cs43131_init_vendor(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate, uint8_t bits)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    if (!s_dev) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = CS43131_I2C_ADDR;
        cfg.scl_speed_hz = 100 * 1000;
        esp_err_t e = i2c_master_bus_add_device(i2c_bus, &cfg, &s_dev);
        if (e != ESP_OK) return e;
    }
    cs43131_set_i2c_dev_handle(s_dev);

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    uint8_t srate = (sample_rate == 32000) ? 0 :
                    (sample_rate == 44100) ? 1 :
                    (sample_rate == 48000) ? 2 :
                    (sample_rate == 88200) ? 3 :
                    (sample_rate == 96000) ? 4 : 1;
    w(0x01000B, srate);
    w(0x01000C, (bits >= 24) ? 0x06 : 0x04);

    w(0x030002, 0x00); w(0x030003, 0x00); w(0x030004, 0x00);
    w(0x030005, 0x00); w(0x030008, 0x00); w(0x03000A, 0x00);
    w(0x030001, 0x01);

    w(0x05000A, (bits >= 24) ? 0x06 : 0x05);
    w(0x05000B, 0x0D); w(0x05000B, 0x06); w(0x05000B, 0x0E);
    w(0x05000B, 0x07); w(0x05000B, 0x0F);

    w(0x020052, 0x04);      /* 晶振偏置 */
    w(0x0F0010, 0xE7);      /* 中断掩码 */

    /* 电源时序：原厂每一步之后都要等时钟就绪，必须轮询，不能裸写 */
    w(0x020000, 0xF6);
    bool xtal = vendor_wait_ready(0x10, 80);      /* XTAL_RDY */
    w(0x020000, 0xF2);
    bool pll = vendor_wait_ready(0x14, 80);       /* XTAL_RDY | PLL_RDY */
    w(0x0F0010, 0xE1);
    ESP_LOGI(TAG, "电源时序: XTAL_READY=%d PLL_READY=%d", xtal ? 1 : 0, pll ? 1 : 0);

    w(0x040010, 0x01); w(0x040011, 0x00); w(0x040012, 0x08); w(0x040013, 0x00);
    w(0x040014, 0x1F); w(0x040015, 0x00); w(0x040016, 0x3F); w(0x040017, 0x00);
    w(0x040018, 0x0C); w(0x040019, 0x0A);
    w(0x050000, 0x00); w(0x050001, 0x00);

    w(0x090000, 0x02);
    w(0x090000, 0x10);
    w(0x090003, 0xE0);
    w(0x090004, 0x00);
    w(0x090001, 0x03);
    w(0x0B0000, 0x1E);
    w(0x080000, 0x30);
    w(0x01000D, 0x02);

    w(0x020000, 0x99);
    vTaskDelay(pdMS_TO_TICKS(20));
    w(0x080032, 0x20);
    vTaskDelay(pdMS_TO_TICKS(15));
    w(0x080032, 0x00);
    w(0x010010, 0x00);

    /* 原厂最终写 0x010006=0x04（外部晶振 22.5792M）。本板用内部 RCO 同频 0x06，
     * 频率相同但不会因晶振未起振导致 I2C 失联。 */
    w(0x010006, 0x06);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t pwr = 0, hp = 0, bs = 0, clk = 0, ch1 = 0, srr = 0, mut = 0;
    r(0x020000, &pwr); r(0x080000, &hp); r(0x01000C, &bs);
    r(0x010006, &clk); r(0x05000A, &ch1); r(0x01000B, &srr); r(0x090003, &mut);
    ESP_LOGI(TAG, "原厂序列完成: PWR=0x%02X HP=0x%02X BITS=0x%02X CLK=0x%02X CH1=0x%02X SRATE=0x%02X MUTE=0x%02X (%lu Hz/%d bit)",
             pwr, hp, bs, clk, ch1, srr, mut, (unsigned long)sample_rate, bits);
    return ESP_OK;
}
