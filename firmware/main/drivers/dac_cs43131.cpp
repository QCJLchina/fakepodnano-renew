/**
 * FAKE_POD_NANO CS43131 DAC 驱动实现
 * ============================================
 * 基于 CS43131 官方寄存器协议与开源初始化时序
 * 接口：I2C 地址 0x30，24 位寄存器地址 + 0x01 控制字节
 */

#include "dac_cs43131.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "CS43131";

static i2c_master_dev_handle_t s_i2c_dev = NULL;

/* ---------------------------------------------------------------
 * 硬件复位引脚操作
 * --------------------------------------------------------------- */
esp_err_t cs43131_hw_reset(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << AUDIO_DAC_RST_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    /* 拉低复位 */
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    /* 拉高启动 */
    gpio_set_level((gpio_num_t)AUDIO_DAC_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "硬件复位引脚 GPIO %d 已释放并就绪", AUDIO_DAC_RST_PIN);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * CS43131 特殊的 I2C 读写时序
 * 24 位地址 + 1 字节控制字节(0x01) + 数据
 * --------------------------------------------------------------- */
static esp_err_t write_reg(uint32_t addr, uint8_t dat)
{
    if (!s_i2c_dev) return ESP_ERR_INVALID_STATE;

    uint8_t buf[5];
    buf[0] = (addr >> 16) & 0xFF;
    buf[1] = (addr >> 8) & 0xFF;
    buf[2] = addr & 0xFF;
    buf[3] = 0x01;  /* 控制字节，固定 0x01 */
    buf[4] = dat;

    esp_err_t ret = i2c_master_transmit(s_i2c_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写寄存器 0x%06lX 失败: %s", (unsigned long)addr, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t read_reg(uint32_t addr, uint8_t *dst)
{
    if (!s_i2c_dev || !dst) return ESP_ERR_INVALID_ARG;

    uint8_t write_buf[4];
    write_buf[0] = (addr >> 16) & 0xFF;
    write_buf[1] = (addr >> 8) & 0xFF;
    write_buf[2] = addr & 0xFF;
    write_buf[3] = 0x01;  /* 控制字节，固定 0x01 */

    esp_err_t ret = i2c_master_transmit_receive(s_i2c_dev, write_buf, sizeof(write_buf), dst, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读寄存器 0x%06lX 失败: %s", (unsigned long)addr, esp_err_to_name(ret));
    }
    return ret;
}

/* ---------------------------------------------------------------
 * 读取芯片 ID
 * --------------------------------------------------------------- */
esp_err_t cs43131_read_chip_id(i2c_master_bus_handle_t i2c_bus, uint8_t *id)
{
    if (!s_i2c_dev) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = CS43131_I2C_ADDR;
        dev_cfg.scl_speed_hz = TOUCH_I2C_CLK_HZ;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_i2c_dev);
        if (ret != ESP_OK) return ret;
    }

    uint8_t b1 = 0, b2 = 0, b3 = 0, rev = 0;
    read_reg(0x000001, &b1);
    read_reg(0x000002, &b2);
    read_reg(0x000003, &b3);
    read_reg(0x000000, &rev);

    ESP_LOGI(TAG, "CS43131 芯片 ID: [0x%02X 0x%02X 0x%02X], 硬件版本 Revision: 0x%02X",
             b1, b2, b3, rev);

    if (id) *id = b2;
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 音量设置：0 ~ 100 映射到 CS43131 硬件衰减
 * CS43131 PCM 音量寄存器 0x090001 / 0x090002：
 * 0x00 = 0dB, 0x10 = -8dB, 0x90 = -72dB, 0xFF = -127.5dB
 * --------------------------------------------------------------- */
esp_err_t cs43131_set_volume(i2c_master_bus_handle_t i2c_bus, uint8_t vol_percent)
{
    if (vol_percent > 100) vol_percent = 100;

    uint8_t reg_val;
    if (vol_percent == 0) {
        reg_val = 0xFF; /* 静音 */
    } else {
        /* 对数/线性平滑映射：100% -> 0x00 (0dB), 1% -> 0x90 (~ -72dB) */
        reg_val = (uint8_t)((100 - vol_percent) * 0x90 / 100);
    }

    write_reg(0x090001, reg_val);
    write_reg(0x090002, reg_val);
    ESP_LOGD(TAG, "设置硬件音量: %d%% (寄存器值: 0x%02X)", vol_percent, reg_val);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 静音设置
 * --------------------------------------------------------------- */
esp_err_t cs43131_set_mute(i2c_master_bus_handle_t i2c_bus, bool mute)
{
    uint8_t tmp = 0;
    read_reg(0x090003, &tmp);
    if (mute) {
        tmp |= 0x03;
    } else {
        tmp &= ~0x03;
    }
    write_reg(0x090003, tmp);
    ESP_LOGI(TAG, "设置静音: %s", mute ? "开" : "关");
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 采样率设置（支持 32k ~ 384k 全阶）
 * --------------------------------------------------------------- */
esp_err_t cs43131_set_sample_rate(i2c_master_bus_handle_t i2c_bus, uint32_t sr)
{
    uint8_t reg = 2; /* 默认 48k */
    switch (sr) {
        case 32000:  reg = 0; break;
        case 44100:  reg = 1; break;
        case 48000:  reg = 2; break;
        case 88200:  reg = 3; break;
        case 96000:  reg = 4; break;
        case 176400: reg = 5; break;
        case 192000: reg = 6; break;
        case 352800: reg = 7; break;
        case 384000: reg = 8; break;
        default:
            ESP_LOGW(TAG, "不支持的采样率 %lu，使用默认 48000", (unsigned long)sr);
            reg = 2;
            break;
    }
    write_reg(0x01000B, reg);
    ESP_LOGI(TAG, "CS43131 采样率设置为: %lu Hz (模式 0x%02X)", (unsigned long)sr, reg);
    return ESP_OK;
}

/* 设置位宽 16 / 24 / 32 bit */
esp_err_t cs43131_set_sample_bits(uint8_t bits)
{
    uint8_t reg = 0;
    switch (bits) {
        case 32: reg = 0; break;
        case 24: reg = 1; break;
        case 16: reg = 2; break;
        case 8:  reg = 3; break;
        default: reg = 0; break;
    }
    write_reg(0x01000C, reg);
    ESP_LOGI(TAG, "CS43131 位宽设置为: %d bit", bits);
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * CS43131 完整硬件启动与配置序列
 * --------------------------------------------------------------- */
esp_err_t cs43131_init(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "开始 CS43131 初始化序列...");

    /* 1. 执行硬件拉低复位 */
    cs43131_hw_reset();

    /* 2. 添加 I2C 设备句柄 */
    if (!s_i2c_dev) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = CS43131_I2C_ADDR;
        dev_cfg.scl_speed_hz = TOUCH_I2C_CLK_HZ;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_i2c_dev));
    }

    /* 3. 读芯片 ID 确认通信正常 */
    uint8_t chip_id = 0;
    cs43131_read_chip_id(i2c_bus, &chip_id);

    /* 4. 晶振与基础时钟驱动 (XTAL Driver) */
    write_reg(0x020052, 0x02); // Crystal setting to 12.5uA
    write_reg(0x010006, 0x06); // Internal MCLK expected 22.5792MHz | RCO Mode
    write_reg(0x020000, 0xF6); // Power up XTAL

    /* 5. PLL 锁相环初始化序列 (标准配置) */
    write_reg(0x020000, 0xF2); // Power up XTAL | PLL
    write_reg(0x040002, 0x03); // PLL_REF_PREDIV = 8
    write_reg(0x030008, 0x0A); // PLL_OUT_DIV = 0x0A
    write_reg(0x030002, 0x80); // PLL_DIV_FRAC
    write_reg(0x030003, 0x76);
    write_reg(0x030004, 0x79);
    write_reg(0x030005, 0x45); // PLL_DIV_INT = 0x45
    write_reg(0x03001B, 0x01); // PLL_MODE = 1
    write_reg(0x03000A, 0x6F); // PLL_CAL_RATIO = 111
    write_reg(0x030001, 0x01); // PLL_START

    /* 6. 将内部数字处理核心切换到 PLL */
    write_reg(0x010006, 0x05); // Internal MCLK -> PLL Mode

    /* 7. ASP 音频串行口配置 (从机模式接收来自 ESP32 I2S 主机的数据) */
    cs43131_set_sample_rate(i2c_bus, 48000);
    cs43131_set_sample_bits(16);

    write_reg(0x01000D, 0x00); // ASP 时钟活跃
    write_reg(0x040018, 0x0C); // ASP Master/Slave 协议与反相设置
    write_reg(0x040019, 0x0A); // 50/50 占空比, 帧起始延迟
    write_reg(0x050000, 0x00); // 通道 1 起始位置
    write_reg(0x050001, 0x00); // 通道 2 起始位置
    write_reg(0x05000A, 0x07); // 32bit / 左右通道激活
    write_reg(0x05000B, 0x0F);

    /* 8. PCM 通路核心控制 */
    write_reg(0x090000, 0x00); // 滤波器模式
    cs43131_set_volume(i2c_bus, 80); // 初始音量 80%
    write_reg(0x090003, 0x0F); // PCM 软斜坡音量过渡
    write_reg(0x090004, 0x00); // 通路极性控制

    write_reg(0x020000, 0xB0); // 电源管理：上电 ASP | XTAL | PLL

    /* 9. 耳机放大器 (HP Driver) 配置 */
    write_reg(0x080000, 0x10); // 满幅输出电压 1V RMS
    write_reg(0x0B0000, 0x00); // 高压模式使能

    /* 10. 免爆音参数 (Pop-free Sequence) */
    write_reg(0x010010, 0x99);
    write_reg(0x080032, 0x20);

    /* 上电耳机功放 */
    write_reg(0x020000, 0xA0); // 上电 HP | ASP | XTAL | PLL
    vTaskDelay(pdMS_TO_TICKS(25));

    /* 恢复免爆音正常状态 */
    write_reg(0x010010, 0x00);
    write_reg(0x080032, 0x00);

    /* 11. 取消静音 */
    cs43131_set_mute(i2c_bus, false);

    ESP_LOGI(TAG, "CS43131 初始化完成，耳放与 PCM 通道已就绪！");
    return ESP_OK;
}
