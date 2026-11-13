/**
 * FAKE_POD_NANO CS43131 DAC 驱动（唯一实现）
 * ============================================
 * 参考实机验证过的时序（a505283042/FakePod_Custom, src/drivers/audio/cs43131.cpp）。
 *
 * 硬件：I2S BCK=GPIO40 WS=GPIO38 DOUT=GPIO39（无 MCLK），RST=GPIO41，I2C=0x30~0x33。
 * 链路：ESP32-S3 作 I2S 主机输出 32bit 槽 → CS43131 作 ASP 从机 → 耳放 → 3.5mm。
 *
 * 两条使用路径：
 *   cs43131_init()           开机一次性：硬件复位 + 探测地址 + 校验 DevId（不开耳放）
 *   cs43131_start_playback() 首次播放/切换采样率：晶振起振 → 32bit ASP → 耳放上电
 *
 * 关键约束（都是实测结论，改动前务必理解）：
 *   1. I2C 控制字节必须是 0x00（8 位访问、地址不自增），不是 0x01
 *   2. 晶振偏置 CRYSTAL_SETTING = 0x06（板载 24.576MHz，7.5uA）
 *   3. 必须先开晶振并等到 INT_STATUS1 bit4=XTAL_READY，再切 SYS_CLOCKING=0x00；
 *      顺序颠倒会让 DAC 数字部分失去时钟、I2C 从机随即失联
 *   4. ASP 固定 32bit 槽：SAMPLE_BITS=0x04、CH1=0x07、CH2=0x0F、LCPR=0x3F；
 *      与 ESP32 I2S 的 32bit 槽必须一致，否则只有底噪没有声音
 *   5. PCM_PATH_CTL_1(0x090003)：静音 0xEF、解除 0xEC（软斜坡位在 0xE0 上，
 *      写 0x00/0x03 会破坏软斜坡配置）
 */

#include "dac_cs43131.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "CS43131";

/* ---------------- 寄存器地址（24 位 MAP） ---------------- */
#define REG_DEVICE_ID_AB        0x010000
#define REG_DEVICE_ID_CD        0x010001
#define REG_DEVICE_ID_E         0x010002
#define REG_REVISION_ID         0x010004
#define REG_SUBREVISION_ID      0x010005
#define REG_SYSTEM_CLOCKING     0x010006
#define REG_ASP_SAMPLE_RATE     0x01000B
#define REG_ASP_SAMPLE_BITS     0x01000C
#define REG_POP_FREE_UP_1       0x010010
#define REG_POWER_DOWN          0x020000
#define REG_CRYSTAL_SETTING     0x020052
#define REG_ASP_N_LSB           0x040010
#define REG_ASP_N_MSB           0x040011
#define REG_ASP_M_LSB           0x040012
#define REG_ASP_M_MSB           0x040013
#define REG_ASP_LCHI_LSB        0x040014
#define REG_ASP_LCHI_MSB        0x040015
#define REG_ASP_LCPR_LSB        0x040016
#define REG_ASP_LCPR_MSB        0x040017
#define REG_ASP_CLOCK_CONFIG    0x040018
#define REG_ASP_FRAME_CONFIG    0x040019
#define REG_ASP_CH1_LOCATION    0x050000
#define REG_ASP_CH2_LOCATION    0x050001
#define REG_ASP_CH1_SIZE_EN     0x05000A
#define REG_ASP_CH2_SIZE_EN     0x05000B
#define REG_HP_OUTPUT_CONTROL   0x080000
#define REG_POP_FREE_UP_2       0x080032
#define REG_PCM_FILTER_OPTION   0x090000
#define REG_PCM_VOLUME_B        0x090001
#define REG_PCM_VOLUME_A        0x090002
#define REG_PCM_PATH_CTL_1      0x090003
#define REG_PCM_PATH_CTL_2      0x090004
#define REG_CLASS_H_CONTROL     0x0B0000
#define REG_INT_STATUS_1        0x0F0000
#define REG_INT_MASK_1          0x0F0010

/* PCM 通路软斜坡：静音/解除静音的唯一合法取值（见文件头约束 5） */
#define PCM_PATH_MUTED          0xEF
#define PCM_PATH_UNMUTED        0xEC

/* 普通耳机安全档：0.5 Vrms（原厂兜底档，高阻/线路输出需另配） */
#define HP_CLASS_H_NORMAL       0x1C
#define HP_OUTPUT_NORMAL        0x00

#define HP_PDN_DONE_WAIT_MS     100

/* ---------------- 唯一 I2C 设备句柄 ---------------- */
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_ready = false;              /* 控制口已验证 */

/* ---------------------------------------------------------------
 * 寄存器读写（控制字节 0x00）
 * --------------------------------------------------------------- */
static esp_err_t write_reg(uint32_t addr, uint8_t val)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[5] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x00,
        val
    };
    for (int i = 0; i < 3; i++) {
        if (i2c_master_transmit(s_dev, buf, sizeof(buf), 100) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGW(TAG, "写 0x%06lX=0x%02X 失败", (unsigned long)addr, val);
    return ESP_FAIL;
}

static esp_err_t read_reg(uint32_t addr, uint8_t *out)
{
    if (!s_dev || !out) return ESP_ERR_INVALID_ARG;
    uint8_t pre[4] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        0x00
    };
    for (int i = 0; i < 3; i++) {
        if (i2c_master_transmit(s_dev, pre, sizeof(pre), 100) == ESP_OK &&
            i2c_master_receive(s_dev, out, 1, 100) == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    *out = 0;
    return ESP_FAIL;
}

/* ---------------------------------------------------------------
 * 采样率 → ASP_SPRATE（与原厂 FakePod Rate Profile 一致）
 * 注意：44.1k 与 48k 同为 2 是原厂档位定义，不是笔误。
 * --------------------------------------------------------------- */
static bool rate_to_sprate(uint32_t sr, uint8_t *out)
{
    switch (sr) {
        case 32000:  *out = 0; return true;
        case 44100:  *out = 2; return true;
        case 48000:  *out = 2; return true;
        case 88200:  *out = 3; return true;
        case 96000:  *out = 4; return true;
        case 176400: *out = 5; return true;
        case 192000: *out = 6; return true;
        default:     return false;
    }
}

/* ---------------------------------------------------------------
 * 硬件复位 + 控制口验证
 * --------------------------------------------------------------- */
static esp_err_t dac_hw_reset(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) return ret;

    /* RESET 低有效；100Hz tick 下 2ms/3ms 会被截断成 0 tick，用 tick 兜底 */
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(3) ? pdMS_TO_TICKS(3) : 1);
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(4) ? pdMS_TO_TICKS(4) : 1);
    return ESP_OK;
}

/* ADR 电阻决定地址低两位，复位后探测真实响应地址（0x30~0x33） */
static esp_err_t probe_device(i2c_master_bus_handle_t bus)
{
    for (uint8_t a = 0x30; a <= 0x33; a++) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = a;
        dev_cfg.scl_speed_hz = 100 * 1000;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) continue;

        s_dev = dev;
        uint8_t id_ab = 0, id_cd = 0, id_e = 0;
        bool ok = (read_reg(REG_DEVICE_ID_AB, &id_ab) == ESP_OK &&
                   read_reg(REG_DEVICE_ID_CD, &id_cd) == ESP_OK &&
                   read_reg(REG_DEVICE_ID_E, &id_e) == ESP_OK &&
                   id_ab == 0x43 && id_cd == 0x13 && id_e == 0x10);
        if (ok) {
            ESP_LOGI(TAG, "发现 CS43131 @0x%02X（DevId 43 13 10）", a);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(dev);
        s_dev = NULL;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t cs43131_init(i2c_master_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;
    if (s_ready) return ESP_OK;

    esp_err_t ret = dac_hw_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "硬件复位失败: %s", esp_err_to_name(ret));
        return ret;
    }

    if (probe_device(i2c_bus) != ESP_OK) {
        ESP_LOGE(TAG, "0x30~0x33 无响应，请检查 RESET 引脚、DAC 供电与 ADR 连接");
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t rev = 0, sub = 0;
    read_reg(REG_REVISION_ID, &rev);
    read_reg(REG_SUBREVISION_ID, &sub);
    ESP_LOGI(TAG, "控制口验证成功：Rev=0x%02X Sub=0x%02X（本阶段不开耳放）", rev, sub);

    s_ready = true;
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 播放链路：晶振 → 时钟 → ASP（幂等，可在采样率变化时重入）
 * --------------------------------------------------------------- */
static esp_err_t start_clock_and_asp(uint32_t sample_rate)
{
    uint8_t sprate = 0;
    if (!rate_to_sprate(sample_rate, &sprate)) {
        ESP_LOGE(TAG, "采样率不在白名单：%lu Hz", (unsigned long)sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* XTAL_READY/XTAL_ERROR 是 sticky 状态：先读清，再解屏蔽 */
    uint8_t st = 0;
    read_reg(REG_INT_STATUS_1, &st);

    if (write_reg(REG_CRYSTAL_SETTING, 0x06) != ESP_OK) return ESP_FAIL;   /* 24.576MHz / 7.5uA */

    st = 0;
    read_reg(REG_INT_STATUS_1, &st);
    uint8_t mask = 0;
    if (read_reg(REG_INT_MASK_1, &mask) != ESP_OK) return ESP_FAIL;
    if (write_reg(REG_INT_MASK_1, (uint8_t)(mask & 0xE7)) != ESP_OK) return ESP_FAIL;

    /* 只开晶振（复位值 0xFE 清 PDN_XTAL 后为 0xF6），耳放与 ASP 仍关闭 */
    uint8_t power = 0;
    if (read_reg(REG_POWER_DOWN, &power) != ESP_OK) return ESP_FAIL;
    if (write_reg(REG_POWER_DOWN, (uint8_t)(power & 0xF6)) != ESP_OK) return ESP_FAIL;

    bool ready = false;
    uint8_t last = 0;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        st = 0;
        if (read_reg(REG_INT_STATUS_1, &st) != ESP_OK) return ESP_FAIL;
        last = st;
        if (st & 0x08) {
            ESP_LOGE(TAG, "晶振启动错误：INT_STATUS1=0x%02X", st);
            return ESP_FAIL;
        }
        if (st & 0x10) { ready = true; break; }
    }
    if (!ready) {
        ESP_LOGE(TAG, "XTAL_READY 超时：INT_STATUS1=0x%02X", last);
        return ESP_ERR_TIMEOUT;
    }

    /* 晶振就绪后才切 Direct MCLK（24.576MHz） */
    if (write_reg(REG_SYSTEM_CLOCKING, 0x00) != ESP_OK) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(1));

    /* ASP：固定 32bit 槽、从机模式 */
    const struct { uint32_t reg; uint8_t val; } cfg[] = {
        { REG_ASP_SAMPLE_RATE,  sprate },
        { REG_ASP_SAMPLE_BITS,  0x04   },
        { REG_ASP_N_LSB,        0x01   },
        { REG_ASP_N_MSB,        0x00   },
        { REG_ASP_M_LSB,        0x08   },
        { REG_ASP_M_MSB,        0x00   },
        { REG_ASP_LCHI_LSB,     0x1F   },
        { REG_ASP_LCHI_MSB,     0x00   },
        { REG_ASP_LCPR_LSB,     0x3F   },
        { REG_ASP_LCPR_MSB,     0x00   },
        { REG_ASP_CLOCK_CONFIG, 0x0C   },
        { REG_ASP_FRAME_CONFIG, 0x0A   },
        { REG_ASP_CH1_LOCATION, 0x00   },
        { REG_ASP_CH2_LOCATION, 0x00   },
        { REG_ASP_CH1_SIZE_EN,  0x07   },
        { REG_ASP_CH2_SIZE_EN,  0x0F   },
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        if (write_reg(cfg[i].reg, cfg[i].val) != ESP_OK) {
            ESP_LOGE(TAG, "配置 ASP 0x%06lX 失败", (unsigned long)cfg[i].reg);
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "ASP 就绪：%lu Hz / 32bit 槽 / 从机", (unsigned long)sample_rate);
    return ESP_OK;
}

/* 耳放 pop-free 上电（PCM 保持静音，由调用方 release） */
static esp_err_t power_up_headphone(void)
{
    const struct { uint32_t reg; uint8_t val; } cfg[] = {
        { REG_PCM_FILTER_OPTION, 0x02   },
        { REG_PCM_VOLUME_B,      0x28   },
        { REG_PCM_VOLUME_A,      0x28   },
        { REG_PCM_PATH_CTL_1,    PCM_PATH_MUTED },
        { REG_PCM_PATH_CTL_2,    0x00   },
        { REG_CLASS_H_CONTROL,   HP_CLASS_H_NORMAL },
        { REG_HP_OUTPUT_CONTROL, HP_OUTPUT_NORMAL },
        { REG_POP_FREE_UP_1,     0x99   },
        { REG_POP_FREE_UP_2,     0x20   },
    };
    for (size_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        if (write_reg(cfg[i].reg, cfg[i].val) != ESP_OK) return ESP_FAIL;
    }

    uint8_t power = 0;
    if (read_reg(REG_POWER_DOWN, &power) != ESP_OK) return ESP_FAIL;

    /* 先确认 ASP 开启，再解除 PDN_HP */
    power &= (uint8_t)(~0x40);
    if (write_reg(REG_POWER_DOWN, power) != ESP_OK) return ESP_FAIL;
    power &= (uint8_t)(~0x10);
    if (write_reg(REG_POWER_DOWN, power) != ESP_OK) return ESP_FAIL;

    vTaskDelay(pdMS_TO_TICKS(12));

    if (write_reg(REG_POP_FREE_UP_2, 0x00) != ESP_OK) return ESP_FAIL;
    if (write_reg(REG_POP_FREE_UP_1, 0x00) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "耳放上电完成（POWER_DOWN=0x%02X，PCM 仍静音）", power);
    return ESP_OK;
}

esp_err_t cs43131_start_playback(i2c_master_bus_handle_t i2c_bus, uint32_t sample_rate)
{
    esp_err_t ret = cs43131_init(i2c_bus);
    if (ret != ESP_OK) return ret;

    ret = start_clock_and_asp(sample_rate);
    if (ret != ESP_OK) return ret;

    return power_up_headphone();
}

/* ---------------------------------------------------------------
 * 音量 / 静音 / 采样率（对外仅此三个控制入口）
 * --------------------------------------------------------------- */
esp_err_t cs43131_set_volume(i2c_master_bus_handle_t i2c_bus, uint8_t volume)
{
    (void)i2c_bus;
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (volume > 100) volume = 100;
    /* 0..100 → 0..0xFF half-dB 衰减；0% 即 0xFF 完全静音 */
    uint8_t att = (uint8_t)((100 - volume) * 0xFF / 100);
    esp_err_t ret = write_reg(REG_PCM_VOLUME_B, att);
    if (ret == ESP_OK) ret = write_reg(REG_PCM_VOLUME_A, att);
    return ret;
}

esp_err_t cs43131_set_mute(i2c_master_bus_handle_t i2c_bus, bool mute)
{
    (void)i2c_bus;
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    /* 唯一允许写 PCM_PATH_CTL_1 的地方：软斜坡位必须保持，只切静音位 */
    return write_reg(REG_PCM_PATH_CTL_1, mute ? PCM_PATH_MUTED : PCM_PATH_UNMUTED);
}

esp_err_t cs43131_release_mute(void)
{
    return cs43131_set_mute(NULL, false);
}
