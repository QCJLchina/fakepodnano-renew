/**
 * FAKE_POD_NANO BLE 低功耗蓝牙无线对时服务实现 (基于 NimBLE)
 */

#include "ble_time_sync.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_TIME";

static bool s_time_synced = false;

/* 16-bit UUID 定义: 0xFFE0 / 0xFFE1（自定义网页对时） */
static const ble_uuid16_t s_svc_uuid  = BLE_UUID16_INIT(0xFFE0);
static const ble_uuid16_t s_char_uuid = BLE_UUID16_INIT(0xFFE1);

/* 标准 Current Time Service: 0x1805 / Current Time 特征 0x2A2B */
static const ble_uuid16_t s_cts_svc_uuid  = BLE_UUID16_INIT(0x1805);
static const ble_uuid16_t s_cts_time_uuid = BLE_UUID16_INIT(0x2A2B);

/* 公历日期 -> 1970 起的秒数（UTC，避免依赖 timegm） */
static time_t cts_to_epoch(int y, int mo, int d, int h, int mi, int s)
{
    y -= (mo <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = (long long)era * 146097 + (long long)doe - 719468;
    return (time_t)(days * 86400 + h * 3600 + mi * 60 + s);
}

/* Current Time 特征：读=返回当前时间；写=按标准格式设置时间 */
static int cts_time_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        uint8_t b[10];
        int year = tmv.tm_year + 1900;
        b[0] = (uint8_t)(year & 0xFF);
        b[1] = (uint8_t)((year >> 8) & 0xFF);
        b[2] = (uint8_t)(tmv.tm_mon + 1);
        b[3] = (uint8_t)tmv.tm_mday;
        b[4] = (uint8_t)tmv.tm_hour;
        b[5] = (uint8_t)tmv.tm_min;
        b[6] = (uint8_t)tmv.tm_sec;
        b[7] = (uint8_t)(tmv.tm_wday == 0 ? 7 : tmv.tm_wday);
        b[8] = 0;
        b[9] = 0;
        int rc = os_mbuf_append(ctxt->om, b, sizeof(b));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len < 10) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        uint8_t b[10];
        os_mbuf_copydata(ctxt->om, 0, 10, b);
        int year = b[0] | (b[1] << 8);
        time_t t = cts_to_epoch(year, b[2], b[3], b[4], b[5], b[6]);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        setenv("TZ", "CST-8", 1);
        tzset();
        s_time_synced = true;
        ESP_LOGI(TAG, "CTS 标准对时写入: %04d-%02d-%02d %02d:%02d:%02d (UTC)",
                 year, b[2], b[3], b[4], b[5], b[6]);
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

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

/* ================= BLE OTA（0xFFF0/1/2/3） ================= */
static const ble_uuid16_t s_ota_svc_uuid  = BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t s_ota_ctrl_uuid = BLE_UUID16_INIT(0xFFF1);
static const ble_uuid16_t s_ota_data_uuid = BLE_UUID16_INIT(0xFFF2);
static const ble_uuid16_t s_ota_stat_uuid = BLE_UUID16_INIT(0xFFF3);

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_ota_part = NULL;
static uint32_t s_ota_total = 0;
static uint32_t s_ota_written = 0;
static volatile uint8_t s_ota_state = 0;   /* 0空闲 1就绪 2写入中 3完成 4错误 */
static uint8_t s_ota_chunk[512];

static int ota_stat_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t b[5];
    b[0] = s_ota_state;
    b[1] = (uint8_t)(s_ota_written & 0xFF);
    b[2] = (uint8_t)((s_ota_written >> 8) & 0xFF);
    b[3] = (uint8_t)((s_ota_written >> 16) & 0xFF);
    b[4] = (uint8_t)((s_ota_written >> 24) & 0xFF);
    int rc = os_mbuf_append(ctxt->om, b, sizeof(b));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void ota_restart_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static int ota_ctrl_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t cmd = 0;
    os_mbuf_copydata(ctxt->om, 0, 1, &cmd);

    if (cmd == 0x01) {   /* 开始 */
        uint32_t total = 0;
        if (len >= 5) {
            uint8_t t[4];
            os_mbuf_copydata(ctxt->om, 1, 4, t);
            total = (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
        }
        s_ota_part = esp_ota_get_next_update_partition(NULL);
        if (!s_ota_part) { s_ota_state = 4; ESP_LOGE(TAG, "无可用 OTA 分区"); return BLE_ATT_ERR_UNLIKELY; }
        esp_err_t err = esp_ota_begin(s_ota_part, total ? total : OTA_SIZE_UNKNOWN, &s_ota_handle);
        if (err != ESP_OK) {
            s_ota_state = 4;
            ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_ota_total = total;
        s_ota_written = 0;
        s_ota_state = 2;
        ESP_LOGI(TAG, "OTA 开始，目标分区 %s，总大小 %lu", s_ota_part->label, (unsigned long)total);
    } else if (cmd == 0x02) {   /* 结束 */
        if (s_ota_state != 2) return BLE_ATT_ERR_UNLIKELY;
        esp_err_t err = esp_ota_end(s_ota_handle);
        if (err != ESP_OK) {
            s_ota_state = 4;
            ESP_LOGE(TAG, "esp_ota_end 失败: %s", esp_err_to_name(err));
            return BLE_ATT_ERR_UNLIKELY;
        }
        err = esp_ota_set_boot_partition(s_ota_part);
        if (err != ESP_OK) {
            s_ota_state = 4;
            ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_ota_state = 3;
        ESP_LOGI(TAG, "OTA 完成，写入 %lu 字节，2 秒后重启", (unsigned long)s_ota_written);
        const esp_timer_create_args_t targs = { .callback = ota_restart_timer_cb, .name = "ota_rst" };
        esp_timer_handle_t th = NULL;
        if (esp_timer_create(&targs, &th) == ESP_OK) {
            esp_timer_start_once(th, 2000000ULL);
        }
    } else if (cmd == 0x03) {   /* 中止 */
        if (s_ota_state == 2) esp_ota_abort(s_ota_handle);
        s_ota_state = 0;
        s_ota_written = 0;
        ESP_LOGW(TAG, "OTA 已中止");
    }
    return 0;
}

static int ota_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (s_ota_state != 2) return BLE_ATT_ERR_UNLIKELY;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0) return 0;
    if (len > sizeof(s_ota_chunk)) len = sizeof(s_ota_chunk);
    os_mbuf_copydata(ctxt->om, 0, len, s_ota_chunk);
    esp_err_t err = esp_ota_write(s_ota_handle, s_ota_chunk, len);
    if (err != ESP_OK) {
        s_ota_state = 4;
        ESP_LOGE(TAG, "esp_ota_write 失败: %s", esp_err_to_name(err));
        return BLE_ATT_ERR_UNLIKELY;
    }
    s_ota_written += len;
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
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_cts_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_cts_time_uuid.u,
                .access_cb = cts_time_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ota_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &s_ota_ctrl_uuid.u, .access_cb = ota_ctrl_access_cb,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = &s_ota_data_uuid.u, .access_cb = ota_data_access_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &s_ota_stat_uuid.u, .access_cb = ota_stat_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
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
    fields.uuids16 = (ble_uuid16_t[]){ s_svc_uuid, s_ota_svc_uuid };
    fields.num_uuids16 = 2;
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

    ble_att_set_preferred_mtu(517);   /* 便于 OTA 大数据块传输 */

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
