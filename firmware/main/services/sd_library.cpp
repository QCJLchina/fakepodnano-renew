/**
 * FAKE_POD_NANO TF 卡曲库实现
 */

#include "sd_library.h"
#include "board_config.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SD_LIB";
static const char *MOUNT = "/sdcard";
static bool s_mounted = false;

static bool ends_with(const char *name, const char *suffix)
{
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    if (n < s) return false;
    for (size_t i = 0; i < s; i++) {
        if (tolower((unsigned char)name[n - s + i]) != suffix[i]) return false;
    }
    return true;
}

static bool playable(const char *name)
{
    return ends_with(name, ".wav") || ends_with(name, ".flac") ||
           ends_with(name, ".mp3") || ends_with(name, ".aac") || ends_with(name, ".m4a") ||
           ends_with(name, ".dsf") || ends_with(name, ".dsd");
}

esp_err_t sd_library_mount(void)
{
    if (s_mounted) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = (gpio_num_t)SD_PIN_CLK;
    slot.cmd = (gpio_num_t)SD_PIN_CMD;
    slot.d0 = (gpio_num_t)SD_PIN_D0;
    slot.d1 = (gpio_num_t)SD_PIN_D1;
    slot.d2 = (gpio_num_t)SD_PIN_D2;
    slot.d3 = (gpio_num_t)SD_PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT, &host, &slot, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TF 卡挂载失败: %s", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "TF 卡已挂载到 %s（FAT16/FAT32/exFAT）", MOUNT);
    return ESP_OK;
}

esp_err_t sd_library_first_track(char *out_path, size_t out_len)
{
    if (!out_path || out_len < 16) return ESP_ERR_INVALID_ARG;
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    DIR *dir = opendir(MOUNT);
    if (!dir) return ESP_FAIL;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!playable(ent->d_name)) continue;
        int n = snprintf(out_path, out_len, "%s/%s", MOUNT, ent->d_name);
        if (n > 0 && (size_t)n < out_len) {
            ESP_LOGI(TAG, "选中曲目: %s", out_path);
            ret = ESP_OK;
        }
        break;
    }
    closedir(dir);
    return ret;
}
