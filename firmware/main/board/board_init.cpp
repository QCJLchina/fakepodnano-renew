/**
 * FAKE_POD_NANO 板级初始化
 * ============================================
 * 按 PINS_CONFIRMED.md 的引脚表初始化各外设
 * 现阶段只实现：日志 + I2C 扫描 + 版本打印
 * 屏幕/触摸在 drivers/ 中逐步加入
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "driver/i2c_master.h"

#include "board_config.h"
#include "board.h"

static const char *TAG = "BOARD";

/* ---------------------------------------------------------------
 * I2C 总线（触摸 CST820 + DAC CS43131 + IMU QMI8658 共用）
 * --------------------------------------------------------------- */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

i2c_master_bus_handle_t board_i2c_bus_get(void)
{
    return s_i2c_bus;
}

static esp_err_t i2c_init(void)
{
    ESP_LOGI(TAG, "初始化 I2C 总线 (SDA=%d, SCL=%d, %d kHz)",
             TOUCH_PIN_SDA, TOUCH_PIN_SCL, TOUCH_I2C_CLK_HZ / 1000);

    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = TOUCH_I2C_HOST;
    cfg.sda_io_num = (gpio_num_t)TOUCH_PIN_SDA;
    cfg.scl_io_num = (gpio_num_t)TOUCH_PIN_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/* 扫描 I2C 总线，确认三个设备是否在线 */
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "扫描 I2C 总线...");
    int found = 0;
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 100) == ESP_OK) {
            const char *name = "未知设备";
            switch (addr) {
                case TOUCH_I2C_ADDR: name = "CST820 触摸"; break;
                case AUDIO_DAC_I2C_ADDR: name = "CS43131 DAC"; break;
                case IMU_I2C_ADDR: name = "QMI8658 陀螺仪"; break;
                default: break;
            }
            ESP_LOGI(TAG, "  发现设备 0x%02X  (%s)", addr, name);
            found++;
        }
    }
    ESP_LOGI(TAG, "扫描完成，共发现 %d 个设备", found);

    if (found < 3) {
        ESP_LOGW(TAG, "预期至少 3 个设备 (0x15/0x30/0x6B)，实际 %d 个，请检查硬件", found);
    }
}

/* ---------------------------------------------------------------
 * 打印芯片与存储信息
 * --------------------------------------------------------------- */
static void print_chip_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "──────── 硬件信息 ────────");
    ESP_LOGI(TAG, "  芯片      : %s rev v%d.%d, %d 核",
             CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100,
             chip.cores);
    ESP_LOGI(TAG, "  CPU 频率  : %d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "  Flash     : %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, "  PSRAM     : %d MB", (int)esp_psram_get_size() / (1024 * 1024));
    ESP_LOGI(TAG, "  空闲内存  : %lu KB",
             (unsigned long)(esp_get_free_heap_size() / 1024));
    ESP_LOGI(TAG, "──────────────────────────");
}

/* ---------------------------------------------------------------
 * 对外接口
 * --------------------------------------------------------------- */
esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════╗");
    ESP_LOGI(TAG, "║   FAKE_POD_NANO  固件启动     ║");
    ESP_LOGI(TAG, "║   HIFI 版 (CS43131)           ║");
    ESP_LOGI(TAG, "╚══════════════════════════════╝");

    print_chip_info();

    esp_err_t err = i2c_init();
    if (err != ESP_OK) {
        return err;
    }
    i2c_scan();

    ESP_LOGI(TAG, "板级初始化完成");
    return ESP_OK;
}
