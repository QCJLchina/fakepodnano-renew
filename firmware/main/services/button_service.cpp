/**
 * FAKE_POD_NANO 物理按键服务实现
 * ============================================
 * 轮询式按键扫描（20ms 采样 + 25ms 去抖），识别单击/双击/长按。
 * 每次电平变化都打印日志，方便确认按键实际接在哪个 GPIO。
 */

#include "button_service.h"
#include "board_config.h"
#include "player_service.h"
#include "ui_manager.h"
#include "usb_uac_service.h"
#include "audio_pipeline.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"

static const char *TAG = "BTN";

/* 用户按键（board_config.h: BTN_PIN=48）；电源键候选 GPIO0（待实测确认） */
#define BTN_FN_PIN    BTN_PIN
#define BTN_PWR_PIN   0

#define POLL_MS       10
#define DEBOUNCE_MS   25
#define LONG_PRESS_MS 1500
#define DOUBLE_GAP_MS 350

static int s_saved_pct = -1;   /* 电源键息屏前的亮度，-1 表示当前亮屏 */

typedef struct {
    int pin;
    const char *name;
    int stable;          /* 去抖后的稳定电平 */
    int cand;            /* 候选电平 */
    int64_t cand_ms;     /* 候选开始时间(ms) */
    int64_t press_ms;    /* 按下时刻(ms) */
    int64_t last_release_ms;
    int clicks;          /* 连续单击计数（用于双击判定） */
} btn_ctx_t;

static void toggle_play_pause(void)
{
    player_status_t st;
    player_get_status(&st);
    if (st.state == PLAYER_STATE_PLAYING) {
        player_pause();
    } else if (st.state == PLAYER_STATE_PAUSED) {
        player_resume();
    } else if (st.current_path[0]) {
        player_resume();
    }
}

/* 音量步进（上键 +5 / 下键 -5），作用于 CS43131 硬件音量 */
static int s_btn_vol = -1;

static void volume_step(int delta)
{
    if (s_btn_vol < 0) {
        player_status_t st;
        player_get_status(&st);
        s_btn_vol = (st.volume > 0) ? st.volume : 80;
    }
    int v = s_btn_vol + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_btn_vol = v;
    audio_pipeline_set_volume((uint8_t)v);
    ESP_LOGI(TAG, "硬件音量 %s -> %d%%", delta > 0 ? "+" : "-", v);
}

/* FN 键短按动作（单击/双击在释放后延时判定） */
static void fn_short_actions(btn_ctx_t *b, int64_t now)
{
    b->clicks++;
    b->last_release_ms = now;
}

static void btn_task(void *arg)
{
    (void)arg;

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << BTN_FN_PIN) | (1ULL << BTN_PWR_PIN);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    btn_ctx_t fn = { .pin = BTN_FN_PIN, .name = "FN", .stable = 1, .cand = 1 };
    btn_ctx_t pw = { .pin = BTN_PWR_PIN, .name = "PWR", .stable = 1, .cand = 1 };
    btn_ctx_t *btns[2] = { &fn, &pw };

    ESP_LOGI(TAG, "按键扫描已启动 (FN=GPIO%d, PWR=GPIO%d)，按下任意键会打印日志", BTN_FN_PIN, BTN_PWR_PIN);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        int64_t now = esp_timer_get_time() / 1000;   /* ms */

        for (int i = 0; i < 2; i++) {
            btn_ctx_t *b = btns[i];
            int lvl = gpio_get_level((gpio_num_t)b->pin);

            if (lvl != b->stable) {
                if (lvl != b->cand) {
                    b->cand = lvl;
                    b->cand_ms = now;
                } else if (now - b->cand_ms >= DEBOUNCE_MS) {
                    b->stable = lvl;
                    if (lvl == 0) {
                        b->press_ms = now;
                        ESP_LOGI(TAG, "%s(GPIO%d) 按下", b->name, b->pin);
                    } else {
                        int64_t dur = now - b->press_ms;
                        ESP_LOGI(TAG, "%s(GPIO%d) 释放，按住 %lld ms", b->name, b->pin, (long long)dur);

                        if (i == 0) {   /* FN 键 */
                            if (dur >= LONG_PRESS_MS) {
                                b->clicks = 0;
                                ESP_LOGI(TAG, "长按 → 打开 USB DAC 页面（启动声卡）");
                                if (usb_uac_service_start() == ESP_OK) {
                                    ui_switch_view(UI_VIEW_UAC);
                                }
                            } else if (dur >= 30) {
                                fn_short_actions(b, now);
                            }
                        } else {        /* PWR 键 */
                            if (dur >= 2500) {
                                ESP_LOGW(TAG, "长按电源键 → 关机（进入深睡，再按电源键开机）");
                                ui_set_backlight_pct(0);
                                vTaskDelay(pdMS_TO_TICKS(200));
                                esp_sleep_enable_ext1_wakeup(1ULL << BTN_PWR_PIN,
                                                             ESP_EXT1_WAKEUP_ANY_LOW);
                                esp_deep_sleep_start();
                            } else if (dur >= 30) {
                                volume_step(+5);   /* 上键短按：音量 + */
                            }
                        }
                    }
                }
            } else {
                b->cand = lvl;
            }
        }

        /* FN 单击/双击判定：双击=下一曲，单击=播放/暂停 */
        if (fn.clicks > 0 && (now - fn.last_release_ms) >= DOUBLE_GAP_MS) {
            if (fn.clicks >= 2) {
                ESP_LOGI(TAG, "双击 → 下一曲");
                player_next();
            } else {
                ESP_LOGI(TAG, "下键单击 → 音量 -");
                volume_step(-5);
            }
            fn.clicks = 0;
        }
    }
}

esp_err_t button_service_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(btn_task, "btn_task", 4096, NULL, 3, NULL, 0);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
