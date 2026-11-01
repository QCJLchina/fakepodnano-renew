/**
 * FAKE_POD_NANO BLE 低功耗蓝牙无线对时服务实现 (基于 NimBLE)
 */

#include "ble_time_sync.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_TIME";

static bool s_time_synced = false;

/* 16-bit UUID 定义: 0xFFE0 / 0xFFE1 */
static const ble_uuid16_t s_svc_uuid  = BLE_UUID16_INIT(0xFFE0);
static const ble_uuid16_t s_char_uuid = BLE_UUID16_INIT(0xFFE1);

/* ---------------------------------------------------------------
 * 处理手机网页端写入的时间包 (8 字节)
 * --------------------------------------------------------------- */
static int time_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 8) {
        ESP_LOGW(TAG, "收到的时间包长度不足 (%d 字节, 需要 8 字节)", len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t packet[8];
    os_mbuf_copydata(ctxt->om, 0, sizeof(packet), packet);

    /* 1. 校验和检验 */
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) {
        sum = (uint8_t)(sum + packet[i]);
    }
    if (sum != packet[7]) {
        ESP_LOGE(TAG, "时间包校验和错误 (计算值: 0x%02X, 收到: 0x%02X)", sum, packet[7]);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 2. 解析时间戳 (小端) */
    uint32_t unix_sec = (uint32_t)packet[0] |
                        ((uint32_t)packet[1] << 8) |
                        ((uint32_t)packet[2] << 16) |
                        ((uint32_t)packet[3] << 24);

    int16_t tz_minutes = (int16_t)((uint16_t)packet[4] | ((uint16_t)packet[5] << 8));

    /* 3. 写入内部高精度 RTC 时钟 */
    struct timeval tv = {
        .tv_sec = (time_t)unix_sec,
        .tv_usec = (suseconds_t)(packet[6] * 4000), /* 毫秒 */
    };
    struct timezone tz = {
        .tz_minuteswest = -tz_minutes,
        .tz_dsttime = 0,
    };
    settimeofday(&tv, &tz);

    /* 设置 CST-8 时区环境变量 */
    setenv("TZ", "CST-8", 1);
    tzset();

    s_time_synced = true;

    /* 打印可读时间确认 */
    time_t now = unix_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "🎉 手机端网页蓝牙对时成功！系统硬件 RTC 当前时间: %s (时区偏移 %+d 分钟)",
             strftime_buf, tz_minutes);

    return 0;
}

/* GATT 服务定义表 */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_char_uuid.u,
                .access_cb = time_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ---------------------------------------------------------------
 * 广播与连接事件管理
 * --------------------------------------------------------------- */
static void ble_advertise(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "手机端蓝牙已建立连接 (status=%d)", event->connect.status);
            if (event->connect.status != 0) {
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "手机端蓝牙已断开连接，重新开启广播等待下一次连接");
            ble_advertise();
            break;

        default:
            break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *device_name = "FAKE_POD_NANO";
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    /* 广播 0xFFE0 服务 UUID，便于手机网页快速过滤发现 */
    fields.uuids16 = (ble_uuid16_t[]){ s_svc_uuid };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);

    ESP_LOGI(TAG, "BLE 无线广播已开启 (设备名: %s)", device_name);
}

static void on_nimble_sync(void)
{
    ESP_LOGI(TAG, "NimBLE 同步就绪，准备广播");
    ble_advertise();
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE 宿主任务启动 (Core 0)");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------------------------------------------------------------
 * 公共对外接口
 * --------------------------------------------------------------- */
esp_err_t ble_time_sync_start(void)
{
    ESP_LOGI(TAG, "初始化 BLE 无线对时服务...");

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb = on_nimble_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    ble_svc_gap_device_name_set("FAKE_POD_NANO");

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE 无线对时服务已就绪！可通过网页 Web Bluetooth 秒级校时！");
    return ESP_OK;
}

esp_err_t ble_time_sync_stop(void)
{
    ble_gap_adv_stop();
    return ESP_OK;
}

bool ble_time_is_synced(void)
{
    return s_time_synced;
}
