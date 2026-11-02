/**
 * FAKE_POD_NANO 主程序
 * ============================================
 * 初始化硬件、屏幕、音频，并在 TF 卡根目录自动播放第一首曲目。
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "board.h"
#include "lcd_co5300.h"
#include "audio_pipeline.h"
#include "player_service.h"
#include "sd_library.h"
#include "ui_manager.h"
#include "usb_uac_service.h"
#include "ble_time_sync.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "===== FAKE_POD_NANO 启动 =====");

    /* 1. NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要擦除，重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 2. 板级初始化（I2C + 信息 + 扫描） */
    err = board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "板级初始化失败: %s", esp_err_to_name(err));
    }

    /* 3. 屏幕与全新 UI 管理器初始化 */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    err = lcd_init(&panel, &io);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "屏幕初始化成功，正在启动全新 UI 界面系统...");
        ui_manager_init(panel, io);
    } else {
        ESP_LOGE(TAG, "屏幕初始化失败: %s", esp_err_to_name(err));
    }

    /* 4. 音频管线初始化 (CS43131 + I2S DMA) */
    err = audio_pipeline_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "音频管线初始化成功 (CS43131 + I2S 就绪)");
    } else {
        ESP_LOGE(TAG, "音频管线初始化失败: %s", esp_err_to_name(err));
    }

    /* 5. 启动音频播放管理服务 (WAV/FLAC/MP3/AAC/DSD) */
    err = player_service_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "本地播放就绪：WAV / FLAC / MP3 / ADTS AAC。裸 M4A 与 DSD 会拒绝");
    } else {
        ESP_LOGE(TAG, "音频播放服务启动失败: %s", esp_err_to_name(err));
    }

    /* 6. 启动 USB 声卡。全速下最高 96 kHz / 24-bit，并与本地播放互斥。 */
    err = usb_uac_service_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "USB 声卡已在线 (44.1k ~ 96kHz, 24-bit)");
    } else {
        ESP_LOGW(TAG, "USB 声卡服务启动跳过或失败: %s", esp_err_to_name(err));
    }

    /* 7. 挂载 TF 卡并播放根目录第一首 wav/flac/mp3/aac。USB 正在占用 I2S 时会拒绝。 */
    err = sd_library_mount();
    if (err == ESP_OK) {
        char track[256];
        if (sd_library_first_track(track, sizeof(track)) == ESP_OK) {
            player_play(track);
        } else {
            ESP_LOGW(TAG, "TF 卡根目录没有可播放的 wav/flac/mp3/aac");
        }
    }

    /* 8. 启动 BLE 无线对时服务 (支持手机浏览器 Web Bluetooth 一键秒连校时) */
    err = ble_time_sync_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BLE 无线对时服务已就绪 (广播名: FAKE_POD_NANO)");
    } else {
        ESP_LOGW(TAG, "BLE 对时服务启动跳过或失败: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "系统全部模块初始化完成，进入主循环");

    uint32_t tick = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        tick += 10;
        ESP_LOGI(TAG, "心跳 %lu 秒 | 空闲内存 %lu KB",
                 (unsigned long)tick,
                 (unsigned long)(esp_get_free_heap_size() / 1024));
    }
}
