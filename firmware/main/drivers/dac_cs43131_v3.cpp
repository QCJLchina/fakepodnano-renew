/**
 * FAKE_POD_NANO CS43131 初始化（v3，依据反汇编结论）
 * ============================================
 * 依据 `_incoming/CS43131_REGISTERS.md` 提取的原厂固件真实操作序列：
 *
 *  1. 硬件复位 → 探测 I2C 0x30~0x33 → 读 DevId(期望 43 13 10) → 读修订号
 *  2. 晶振偏置：读 page15/0x10 → val = old & 0xFE → 写回
 *     （前置：page11/0x00 ← 0x00、page8/0x00 ← 0x00）
 *  3. 启动晶振：page15/0x00 ← 0x00
 *  4. 轮询 INT_STATUS1 等 XTAL_READY（20ms 超时）
 *  5. 切换到 24.576MHz 直接时钟（page1/0x06 的 MCLK 源选择）
 *  6. 配置 ASP 寄存器
 *  7. 配置耳放档位（普通耳机安全档）：classH=0x20→page1/0x10，hp=0x1C→page8/0x00
 *     配套：page8/0x32←0xEF、page11/0x00←0x20、page9/0x01~0x04←0x28
 *  8. 轮询 PDN_DONE 等耳放稳定
 *
 * 寄存器编号编码：reg_id = (page << 16) | reg_offset
 */

#include "dac_cs43131.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "CS43131V3";

static i2c_master_dev_handle_t s_dev = NULL;

/* ---------------- 基础读写 ---------------- */
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

/* SP_SRATE：0=32k 1=44.1k 2=48k 3=88.2k 4=96k 5=176.4k 6=192k */
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

/* SP_BITSIZE：0=32bit 1=24bit 2=16bit */
static uint8_t bitsize_val(uint8_t bits)
{
    switch (bits) {
        case 16: return 0x02;
        case 24: return 0x01;
        case 32: return 0x00;
        default: return 0x02;
    }
}

/* 通道位宽编码：16bit=0x01 24bit=0x02 32bit=0x03；0x04=CH_EN */
static uint8_t ch_size_val(uint8_t bits)
{
    switch (bits) {
        case 16: return 0x01;
        case 24: return 0x02;
        case 32: return 0x03;
        default: return 0x01;
    }
}

/* 在 0x30~0x33 中探测设备（原厂行为） */
static esp_err_t probe_device(i2c_master_bus_handle_t bus)
{
    for (uint8_t addr = 0x30; addr <= 0x33; addr++) {
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = addr;
        cfg.scl_speed_hz = 100 * 1000;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) continue;

        uint8_t a[4] = { 0x01, 0x00, 0x00, 0x01 };   /* 读 DevId: page1/0x00 */
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

/* 时钟建立（严格按原厂顺序，顺序错了会让 I2C 从机停摆）：
 *   1) 写 CRYSTAL_SET(0x020052)=0x04 使能 24.576MHz 晶振偏置
 *   2) 轮询 INT_STATUS_1(0x0F0000) bit4 = XTAL_READY（最多 100ms）
 *   3) 就绪后才把 SYS_CLK_CTL_1(0x010006) 切到外部晶振直接时钟(0x00)
 *   4) 切完立即回读一次 DevId 验证 I2C 没死；若失败则安全回退到 RCO(0x06)
 */
static esp_err_t clock_setup(void)
{
    /* 1. 使能晶振偏置 */
    w(0x020052, 0x04);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 2. 等 XTAL_READY：先固定等 400ms（实测晶振起振约 200~400ms），
     *    再读确认。轮询过程里紧跟着写操作读 INT_STATUS 容易出现读失败，
     *    所以改成"先延时、后确认"。 */
    vTaskDelay(pdMS_TO_TICKS(400));
    bool ready = false;
    uint8_t st = 0;
    for (int i = 0; i < 10; i++) {
        if (r(0x0F0000, &st) == ESP_OK && (st & 0x10)) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "晶振: INT_STATUS1=0x%02X, XTAL_READY=%d", st, ready ? 1 : 0);
    if (!ready) {
        ESP_LOGW(TAG, "晶振未就绪，保持内部 RCO（可能无声）");
        w(0x010006, 0x06);
        return ESP_ERR_TIMEOUT;
    }

    /* 3. 切到外部晶振直接时钟 */
    w(0x010006, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 4. 验证 I2C 是否还活着 */
    uint8_t id_check = 0;
    bool alive = false;
    for (int i = 0; i < 5; i++) {
        if (r(0x010000, &id_check) == ESP_OK && id_check == 0x43) { alive = true; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!alive) {
        ESP_LOGE(TAG, "切外部时钟后 I2C 失联，回退 RCO");
        w(0x010006, 0x06);
        vTaskDelay(pdMS_TO_TICKS(10));
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "已切换到 24.576MHz 晶振直接时钟（SYS_CLK_CTL_1=0x00）");
    return ESP_OK;
}

/* ---------------- ASP 参数扫描表 ----------------
 * 每项：{ SP_SRATE(0x01000B), ASP_CLOCK_CONF(0x040018), ASP_FRAME_CONF(0x040019) }
 * SP_SRATE = STP(0x10) | 5050(0x08) | FSD(0x00..0x06, 48k=2)
 * ASP_CLOCK_CONF bit4=模式(0=从)、bit3=SCPOL_OUT、bit2=SCPOL_IN、bit1=LCPOL_OUT、bit0=LCPOL_IN
 */
static const uint8_t kAspTable[][3] = {
    { 0x02, 0x0C, 0x0A },   /* 0: 当前值（Linux 默认） */
    { 0x02, 0x08, 0x0A },   /* 1: 只翻转 SCPOL_OUT */
    { 0x02, 0x04, 0x0A },   /* 2: 只翻转 SCPOL_IN */
    { 0x02, 0x00, 0x0A },   /* 3: 极性全不翻转 */
    { 0x02, 0x0C, 0x00 },   /* 4: 帧格式 0x00 */
    { 0x02, 0x0C, 0x02 },   /* 5: 帧格式 0x02 */
    { 0x0A, 0x0C, 0x0A },   /* 6: SP_SRATE 加 5050 */
    { 0x12, 0x0C, 0x0A },   /* 7: SP_SRATE 加 STP */
};

int cs43131_asp_count(void)
{
    return (int)(sizeof(kAspTable) / sizeof(kAspTable[0]));
}

esp_err_t cs43131_asp_try(int idx)
{
    if (idx < 0 || idx >= cs43131_asp_count() || !s_dev) return ESP_ERR_INVALID_ARG;
    w(0x01000B, kAspTable[idx][0]);
    w(0x040018, kAspTable[idx][1]);
    w(0x040019, kAspTable[idx][2]);
    ESP_LOGI(TAG, "ASP[%d]: SRATE=0x%02X CLK=0x%02X FRAME=0x%02X",
             idx, kAspTable[idx][0], kAspTable[idx][1], kAspTable[idx][2]);
    return ESP_OK;
}

esp_err_t cs43131_set_word_bits(uint8_t bits)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t sp  = bitsize_val(bits);                 /* 0=32bit 1=24bit 2=16bit */
    uint8_t ch  = (uint8_t)(ch_size_val(bits) | 0x04);
    uint8_t den = (bits <= 16) ? 16 : 8;

    w(0x01000C, sp);
    w(0x05000A, ch);
    w(0x05000B, ch);
    w(0x040010, 0x01); w(0x040011, 0x00);
    w(0x040012, den);  w(0x040013, 0x00);
    ESP_LOGI(TAG, "位宽切换: bits=%d SP_BITSIZE=0x%02X CH_SZ=0x%02X DEN=%d", bits, sp, ch, den);
    return ESP_OK;
}

esp_err_t cs43131_init_v3(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate, uint8_t bits)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    /* 1. 硬件复位（拉低 15ms） */
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    io.mode = GPIO_MODE_OUTPUT;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    /* 2. 探测 I2C 地址并读 DevId */
    if (probe_device(i2c_bus) != ESP_OK) return ESP_ERR_NOT_FOUND;
    cs43131_set_i2c_dev_handle(s_dev);

    uint8_t rev = 0;
    r(0x010004, &rev);
    ESP_LOGI(TAG, "CS43131 就绪（Revision=0x%02X）", rev);

    /* 3. 先上电（0x020000 写 0xA0 会清掉 PDN_XTAL/PDN_PLL/PDN_ASP/PDN_HP，
     *    晶振必须在上电之后才会起振、XTAL_READY 才会置位） */
    w(0x020000, 0xA0);
    vTaskDelay(pdMS_TO_TICKS(30));

    /* 4. 上电后再等晶振就绪，然后切 24.576MHz 直接时钟 */
    esp_err_t clk_err = clock_setup();

    /* 4. 串口位宽/采样率 */
    w(0x01000B, srate_idx(sample_rate));
    w(0x01000C, bitsize_val(bits));
    w(0x01000D, 0x03);            /* PAD_INT_CFG 默认 */

    /* 6. ASP：从模式（bit4=0）、I2S 极性
     *    NUM/DEN 必须按「MCLK : 帧长」计算，否则内部不锁频：
     *      MCLK = 24.576MHz，帧长 = 每声道位宽 × 2 声道
     *      16bit 数据 → 32bit 帧 → 24576000/48000 = 512 = NUM/DEN 的基准，DEN=16
     *      24/32bit 数据 → 64bit 帧 → DEN=8
     */
    uint8_t den = (bits <= 16) ? 16 : 8;
    w(0x040018, 0x0C);
    w(0x040019, 0x0A);
    w(0x040010, 0x01); w(0x040011, 0x00);   /* ASP_NUM = 1 */
    w(0x040012, den);  w(0x040013, 0x00);   /* ASP_DEN = 16 或 8 */
    w(0x040014, 0x1F); w(0x040015, 0x00);
    w(0x040016, 0x3F); w(0x040017, 0x00);
    ESP_LOGI(TAG, "ASP 配置: NUM=1 DEN=%d（帧长 %d bit）", den, (bits <= 16) ? 32 : 64);

    /* 6. ASP 通道位宽 + 使能 */
    uint8_t ch = (uint8_t)(ch_size_val(bits) | 0x04);
    w(0x05000A, ch);
    w(0x05000B, ch);

    /* 8. 耳放档位（普通耳机安全档，原厂兜底档）：classH=0x20, hp=0x1C
     *    配套寄存器：page8/0x32←0xEF、page11/0x00←0x20、page9/0x01~0x04←0x28
     */
    w(0x090001, 0x28);            /* PCM Volume B */
    w(0x090002, 0x28);            /* PCM Volume A */
    w(0x090003, 0x28);            /* PCM Path Ctl 1 */
    w(0x090004, 0x28);            /* PCM Path Ctl 2 */
    w(0x0B0000, 0x20);            /* page11/0x00 */
    w(0x080000, 0x1C);            /* hp  */
    w(0x010010, 0x20);            /* classH */
    w(0x080032, 0xEF);            /* page8/0x32 */

    /* 9. 等耳放稳定 */
    vTaskDelay(pdMS_TO_TICKS(120));

    /* 9. 回读诊断 */
    {
        uint8_t pwr = 0, hp = 0, cls = 0, volA = 0, bits_r = 0, ch1 = 0;
        uint8_t is1 = 0, is2 = 0, is3 = 0, is4 = 0, clk = 0, xtal = 0, hpst = 0;
        r(0x020000, &pwr);
        r(0x080000, &hp);
        r(0x010010, &cls);
        r(0x090002, &volA);
        r(0x01000C, &bits_r);
        r(0x05000A, &ch1);
        r(0x0F0000, &is1);
        r(0x0F0001, &is2);
        r(0x0F0002, &is3);
        r(0x0F0003, &is4);
        r(0x010006, &clk);
        r(0x020052, &xtal);
        r(0x0D0001, &hpst);
        ESP_LOGI(TAG, "回读: PWR=0x%02X HP=0x%02X classH=0x%02X VOL_A=0x%02X BITS=0x%02X CH1=0x%02X",
                 pwr, hp, cls, volA, bits_r, ch1);
        ESP_LOGI(TAG, "时钟: SYS_CLK_CTL1=0x%02X CRYSTAL_SET=0x%02X", clk, xtal);
        ESP_LOGI(TAG, "中断: IS1=0x%02X IS2=0x%02X IS3=0x%02X IS4=0x%02X HP_STATUS=0x%02X",
                 is1, is2, is3, is4, hpst);
    }

    if (clk_err != ESP_OK) {
        ESP_LOGE(TAG, "时钟未就绪（err=%s）→ 很可能无声", esp_err_to_name(clk_err));
    } else {
        ESP_LOGI(TAG, "初始化完成（%lu Hz / %d bit，24.576MHz 晶振模式）",
                 (unsigned long)sample_rate, bits);
    }
    return ESP_OK;
}
