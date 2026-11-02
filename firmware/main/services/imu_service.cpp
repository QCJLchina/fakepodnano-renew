/**
 * FAKE_POD_NANO IMU 手势服务实现（QMI8658）
 */

#include "imu_service.h"
#include "board.h"
#include "board_config.h"
#include "player_service.h"
#include "ui_manager.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "IMU";

/* QMI8658 寄存器 */
#define QMI_WHO_AM_I   0x00
#define QMI_CTRL1      0x02
#define QMI_CTRL2      0x03
#define QMI_CTRL7      0x08
#define QMI_AX_L       0x35

static i2c_master_dev_handle_t s_imu_dev = NULL;
static volatile bool s_enabled = true;
static volatile bool s_present = false;

void imu_service_set_enabled(bool enabled)
{
    s_enabled = enabled;
    ESP_LOGI(TAG, "IMU 手势已%s", enabled ? "开启" : "关闭");
}

bool imu_service_get_enabled(void)
{
    return s_enabled;
}

bool imu_service_present(void)
{
    return s_present;
}

/* 手势参数（可按实测调整） */
#define TILT_G           0.45f     /* 倾斜判定阈值(g) */
#define TILT_HOLD_MS     220       /* 持续倾斜时间 */
#define TILT_COOLDOWN_MS 600       /* 两次倾斜动作最小间隔 */
#define TAP_G            1.10f     /* 敲击：合加速度偏离 1g 的阈值(g) */
#define TAP_DOUBLE_MS    400       /* 双击窗口 */

static esp_err_t qmi_write(uint8_t reg, uint8_t val)
{
    if (!s_imu_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_imu_dev, buf, sizeof(buf), 50);
}

static esp_err_t qmi_read(uint8_t reg, uint8_t *dst, size_t len)
{
    if (!s_imu_dev || !dst) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(s_imu_dev, &reg, 1, dst, len, 50);
}

static void imu_task(void *arg)
{
    (void)arg;

    i2c_master_bus_handle_t bus = board_i2c_bus_get();
    if (!bus) {
        ESP_LOGE(TAG, "I2C 总线未初始化，IMU 不可用");
        vTaskDelete(NULL);
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = IMU_I2C_ADDR;
    dev_cfg.scl_speed_hz = 100 * 1000;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_imu_dev) != ESP_OK) {
        ESP_LOGE(TAG, "添加 IMU I2C 设备失败");
        vTaskDelete(NULL);
        return;
    }

    uint8_t who = 0;
    if (qmi_read(QMI_WHO_AM_I, &who, 1) == ESP_OK) {
        s_present = true;
        ESP_LOGI(TAG, "QMI8658 WHO_AM_I = 0x%02X (期望 0x05)", who);
    } else {
        s_present = false;
        ESP_LOGW(TAG, "未检测到 IMU（WHO_AM_I 读取失败），手势功能不可用");
    }

    /* CTRL1: 地址自增 | CTRL2: ±4g, ~250Hz | CTRL7: 使能加速度计 */
    qmi_write(QMI_CTRL1, 0x40);
    qmi_write(QMI_CTRL2, 0x12);
    qmi_write(QMI_CTRL7, 0x01);
    vTaskDelay(pdMS_TO_TICKS(60));

    /* ±4g -> 8192 LSB/g */
    const float scale = 1.0f / 8192.0f;

    int tilt_dir = 0;          /* 当前持续倾斜方向: 0无 1左 2右 3上 4下 */
    int64_t tilt_since = 0;
    int64_t last_action = 0;
    int64_t last_tap = 0;
    int tap_count = 0;
    int64_t log_tick = 0;
    int64_t last_mag_event = 0;

    ESP_LOGI(TAG, "IMU 手势已启动（左右倾切歌 / 上下倾音量 / 双击播放暂停）");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));   /* 50Hz */

        if (!s_enabled) continue;   /* 用户在设置里关闭手势识别 */

        uint8_t raw[6] = {0};
        if (qmi_read(QMI_AX_L, raw, sizeof(raw)) != ESP_OK) continue;

        int16_t ax = (int16_t)((raw[1] << 8) | raw[0]);
        int16_t ay = (int16_t)((raw[3] << 8) | raw[2]);
        int16_t az = (int16_t)((raw[5] << 8) | raw[4]);

        float gx = ax * scale;
        float gy = ay * scale;
        float gz = az * scale;
        float mag = sqrtf(gx * gx + gy * gy + gz * gz);

        int64_t now = esp_timer_get_time() / 1000;

        /* 每 1 秒打印一次原始值，便于校准方向 */
        if (now - log_tick >= 1000) {
            log_tick = now;
            ESP_LOGI(TAG, "accel x=%.2fg y=%.2fg z=%.2fg |mag|=%.2fg", gx, gy, gz, mag);
        }

        /* 敲击/双击检测 */
        if (fabsf(mag - 1.0f) > TAP_G && (now - last_mag_event) > 120) {
            last_mag_event = now;
            if (now - last_tap < TAP_DOUBLE_MS) {
                tap_count++;
            } else {
                tap_count = 1;
            }
            last_tap = now;
        }

        if (tap_count >= 2 && (now - last_tap) < TAP_DOUBLE_MS) {
            tap_count = 0;
            if (ui_get_current_view() == UI_VIEW_PLAYER) {
                player_status_t st;
                player_get_status(&st);
                if (st.state == PLAYER_STATE_PLAYING) {
                    player_pause();
                } else if (st.state == PLAYER_STATE_PAUSED) {
                    player_resume();
                }
                ESP_LOGI(TAG, "手势: 双击 → 播放/暂停");
            }
        } else if (tap_count > 0 && (now - last_tap) >= TAP_DOUBLE_MS) {
            tap_count = 0;
        }

        /* 倾斜检测（仅播放页响应） */
        int dir = 0;
        if (gx > TILT_G) dir = 1;        /* 左倾(待校准) */
        else if (gx < -TILT_G) dir = 2;  /* 右倾 */
        else if (gy > TILT_G) dir = 3;   /* 上倾 */
        else if (gy < -TILT_G) dir = 4;  /* 下倾 */

        if (dir != tilt_dir) {
            tilt_dir = dir;
            tilt_since = now;
        } else if (dir != 0 && (now - tilt_since) >= TILT_HOLD_MS &&
                   (now - last_action) >= TILT_COOLDOWN_MS) {
            if (ui_get_current_view() == UI_VIEW_PLAYER) {
                player_status_t st;
                player_get_status(&st);
                int v = st.volume;
                if (dir == 1) { player_prev(); ESP_LOGI(TAG, "手势: 左倾 → 上一曲"); }
                else if (dir == 2) { player_next(); ESP_LOGI(TAG, "手势: 右倾 → 下一曲"); }
                else if (dir == 3) { v = (v + 5 > 100) ? 100 : v + 5; player_set_volume((uint8_t)v); ESP_LOGI(TAG, "手势: 上倾 → 音量 %d%%", v); }
                else if (dir == 4) { v = (v - 5 < 0) ? 0 : v - 5; player_set_volume((uint8_t)v); ESP_LOGI(TAG, "手势: 下倾 → 音量 %d%%", v); }
            }
            last_action = now;
            tilt_since = now;
        }
    }
}

esp_err_t imu_service_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(imu_task, "imu_task", 4096, NULL, 3, NULL, 0);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
