/**
 * FAKE_POD_NANO USB U盘(MSC)模式实现
 * 仅在 CONFIG_FAKEPOD_USB_MSC_MODE=y 的固件变体中编译。
 */

#include "sdkconfig.h"

#if CONFIG_FAKEPOD_USB_MSC_MODE

#include "usb_msc_dev.h"

#include "tusb.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include <string.h>

/* TF 卡 SDMMC 引脚（与 main/board/board_config.h 保持一致；
 * 独立组件不能反向依赖 main，故在此本地定义） */
#define SD_PIN_CLK  16
#define SD_PIN_CMD  17
#define SD_PIN_D0   15
#define SD_PIN_D1   14
#define SD_PIN_D2   21
#define SD_PIN_D3   18

static const char *TAG = "USB_MSC";
static sdmmc_card_t s_card_storage;
static sdmmc_card_t *s_card = NULL;

esp_err_t usb_msc_mode_start(void)
{
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

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "sdmmc_host_init 失败: %s", esp_err_to_name(err)); return err; }

    err = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot);
    if (err != ESP_OK) { ESP_LOGE(TAG, "sdmmc_host_init_slot 失败: %s", esp_err_to_name(err)); return err; }

    err = sdmmc_card_init(&host, &s_card_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TF 卡初始化失败: %s（请插卡后重启）", esp_err_to_name(err));
        return err;
    }
    s_card = &s_card_storage;
    ESP_LOGI(TAG, "TF 卡就绪，容量 %llu 扇区", (unsigned long long)s_card->csd.capacity);

    if (!tusb_init()) {
        ESP_LOGE(TAG, "tusb_init 失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "USB U盘(MSC) 模式已启动，插电脑应出现可移动磁盘");
    return ESP_OK;
}

/* ---------- TinyUSB MSC 回调 ---------- */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun; (void)offset;
    if (!s_card) return 0;
    uint32_t nsec = bufsize / 512;
    if (nsec == 0) return 0;
    if (sdmmc_read_sectors(s_card, buffer, lba, nsec) != ESP_OK) return 0;
    return bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun; (void)offset;
    if (!s_card) return 0;
    uint32_t nsec = bufsize / 512;
    if (nsec == 0) return 0;
    if (sdmmc_write_sectors(s_card, buffer, lba, nsec) != ESP_OK) return 0;
    return bufsize;
}

/* 容量查询（TinyUSB 内置 READ_CAPACITY 走这里） */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = s_card ? s_card->csd.capacity : 0;
    *block_size = 512;
}

/* 介质就绪判断 */
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return s_card != NULL;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)lun;
    int32_t res = -1;

    switch (scsi_cmd[0]) {
        case SCSI_CMD_TEST_UNIT_READY:
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        case SCSI_CMD_READ_10:
        case SCSI_CMD_WRITE_10:
            res = 0;
            break;

        case SCSI_CMD_INQUIRY: {
            uint8_t *p = (uint8_t *)buffer;
            memset(p, 0, bufsize);
            if (bufsize >= 36) {
                p[0] = 0x00;  /* 直接存取块设备 */
                p[2] = 0x05;  /* SPC-2 */
                p[3] = 0x02;  /* 响应格式 */
                p[4] = 31;    /* 附加长度 */
                memcpy(p + 8, "FAKEPOD ", 8);
                memcpy(p + 16, "MSC Storage     ", 16);
                memcpy(p + 32, "1.00", 4);
                res = 36;
            } else {
                res = bufsize;
            }
            break;
        }

        case SCSI_CMD_READ_CAPACITY_10: {
            uint32_t last_lba = (s_card && s_card->csd.capacity) ? (s_card->csd.capacity - 1) : 0;
            uint32_t bsz = 512;
            uint8_t *p = (uint8_t *)buffer;
            p[0] = (uint8_t)(last_lba >> 24);
            p[1] = (uint8_t)(last_lba >> 16);
            p[2] = (uint8_t)(last_lba >> 8);
            p[3] = (uint8_t)last_lba;
            p[4] = (uint8_t)(bsz >> 24);
            p[5] = (uint8_t)(bsz >> 16);
            p[6] = (uint8_t)(bsz >> 8);
            p[7] = (uint8_t)bsz;
            res = 8;
            break;
        }

        case SCSI_CMD_REQUEST_SENSE: {
            uint8_t *p = (uint8_t *)buffer;
            memset(p, 0, bufsize);
            if (bufsize >= 18) { p[0] = 0x70; p[7] = 0x0A; res = 18; }
            else res = bufsize;
            break;
        }

        default:
            res = -1;
            break;
    }
    return res;
}

#endif /* CONFIG_FAKEPOD_USB_MSC_MODE */
