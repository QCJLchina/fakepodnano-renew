/**
 * FAKE_POD_NANO USB 声卡专属界面实现
 */

#include "ui_view_uac.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "usb_uac_service.h"
#include "audio_pipeline.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>

static lv_obj_t *s_screen_uac = NULL;
static lv_obj_t *s_lbl_rate_big = NULL;
static lv_obj_t *s_lbl_stream_state = NULL;
static lv_obj_t *s_lbl_diag = NULL;
static lv_obj_t *s_bars[8];
static uint64_t s_last_rx = 0;

static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void uac_reboot_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_lbl_diag) lv_label_set_text(s_lbl_diag, "正在重启以释放 USB…");
    const esp_timer_create_args_t targs = { .callback = reboot_cb, .name = "uac_reboot" };
    esp_timer_handle_t th = NULL;
    if (esp_timer_create(&targs, &th) == ESP_OK) {
        esp_timer_start_once(th, 250000ULL);
    } else {
        esp_restart();
    }
}

static void uac_home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

lv_obj_t* ui_view_uac_create(void)
{
    s_screen_uac = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_uac, lv_color_black(), 0);

    /* 顶部标题栏 */
    lv_obj_t *title = lv_label_create(s_screen_uac);
    lv_obj_set_style_text_color(title, lv_color_make(140, 140, 160), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, "USB AUDIO CLASS 2.0 DAC");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* 右上角：退出并重启（释放被声卡占用的 USB，恢复 COM3 / 普通模式） */
    lv_obj_t *btn_exit = lv_btn_create(s_screen_uac);
    lv_obj_set_size(btn_exit, 118, 40);
    lv_obj_align(btn_exit, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_set_style_radius(btn_exit, 10, 0);
    lv_obj_set_style_bg_color(btn_exit, lv_color_make(42, 32, 18), 0);
    lv_obj_set_style_shadow_width(btn_exit, 0, 0);
    lv_obj_add_event_cb(btn_exit, uac_reboot_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_obj_set_style_text_font(lbl_exit, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl_exit, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(lbl_exit, "退出重启");
    lv_obj_center(lbl_exit);

    /* 醒目的大字采样率指示 */
    s_lbl_rate_big = lv_label_create(s_screen_uac);
    lv_obj_set_style_text_color(s_lbl_rate_big, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_style_text_font(s_lbl_rate_big, &lv_font_montserrat_32, 0);
    lv_label_set_text(s_lbl_rate_big, "-- kHz");
    lv_obj_align(s_lbl_rate_big, LV_ALIGN_CENTER, 0, -40);

    /* 采样位宽与通道指示 */
    lv_obj_t *desc = lv_label_create(s_screen_uac);
    lv_obj_set_style_text_color(desc, lv_color_white(), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_16, 0);
    lv_label_set_text(desc, "24-bit / stereo, up to 96 kHz");
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, 5);

    /* 动态音频推流状态指示 */
    s_lbl_stream_state = lv_label_create(s_screen_uac);
    lv_obj_set_style_text_color(s_lbl_stream_state, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_text_font(s_lbl_stream_state, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_lbl_stream_state, "● Streaming");
    lv_obj_align(s_lbl_stream_state, LV_ALIGN_CENTER, 0, 40);

    /* 屏显诊断：硬件音量 / 已接收数据量 / 数据流是否在动 */
    s_lbl_diag = lv_label_create(s_screen_uac);
    lv_obj_set_style_text_color(s_lbl_diag, lv_color_make(180, 180, 195), 0);
    lv_obj_set_style_text_font(s_lbl_diag, &ui_font_cjk_16, 0);
    lv_label_set_text(s_lbl_diag, "音量 -- | 已收 -- | 无数据");
    lv_obj_align(s_lbl_diag, LV_ALIGN_CENTER, 0, 72);

    /* 底部频谱动感柱状条 (Visualizer Bars) */
    lv_obj_t *bar_container = lv_obj_create(s_screen_uac);
    lv_obj_set_size(bar_container, 320, 80);
    lv_obj_align(bar_container, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_bg_opa(bar_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar_container, 0, 0);
    lv_obj_set_style_pad_all(bar_container, 0, 0);
    lv_obj_set_flex_flow(bar_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 8; i++) {
        s_bars[i] = lv_bar_create(bar_container);
        lv_obj_set_size(s_bars[i], 24, 70);
        lv_obj_set_style_bg_color(s_bars[i], lv_color_make(30, 30, 40), 0);
        lv_obj_set_style_bg_color(s_bars[i], lv_palette_main(LV_PALETTE_CYAN), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_bars[i], 4, 0);
        lv_obj_set_style_radius(s_bars[i], 4, LV_PART_INDICATOR);
        lv_bar_set_range(s_bars[i], 0, 100);
        lv_bar_set_value(s_bars[i], 15 + i * 10, LV_ANIM_OFF);
    }

    /* 左上角「桌面」返回键（UI_DESIGN_SPEC：一键回桌面） */
    lv_obj_t *btn_home = lv_btn_create(s_screen_uac);
    lv_obj_set_size(btn_home, 76, 40);
    lv_obj_align(btn_home, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_radius(btn_home, 10, 0);
    lv_obj_set_style_bg_color(btn_home, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_home, 0, 0);
    lv_obj_add_event_cb(btn_home, uac_home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_obj_set_style_text_font(lbl_home, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_home, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(lbl_home, LV_SYMBOL_HOME);
    lv_obj_center(lbl_home);

    return s_screen_uac;
}

void ui_view_uac_update(void)
{
    if (!s_screen_uac) return;

    uint32_t sr = usb_uac_get_current_sample_rate();
    char buf[32];
    if (sr >= 1000) {
        snprintf(buf, sizeof(buf), "%.1f kHz", (float)sr / 1000.0f);
    } else {
        snprintf(buf, sizeof(buf), "%lu Hz", (unsigned long)sr);
    }
    lv_label_set_text(s_lbl_rate_big, buf);

    bool is_streaming = usb_uac_is_streaming();
    if (is_streaming) {
        lv_label_set_text(s_lbl_stream_state, "● Active Streaming");
        lv_obj_set_style_text_color(s_lbl_stream_state, lv_palette_main(LV_PALETTE_GREEN), 0);

        /* 动态模拟动态柱跳动效果 */
        static int phase = 0;
        phase++;
        for (int i = 0; i < 8; i++) {
            int val = 20 + ((phase * 7 + i * 23) % 75);
            lv_bar_set_value(s_bars[i], val, LV_ANIM_ON);
        }
    } else {
        lv_label_set_text(s_lbl_stream_state, "○ Standby / Idle");
        lv_obj_set_style_text_color(s_lbl_stream_state, lv_color_make(120, 120, 140), 0);
        for (int i = 0; i < 8; i++) {
            lv_bar_set_value(s_bars[i], 5, LV_ANIM_ON);
        }
    }

    /* 屏显诊断：硬件音量 / 累计接收 / 是否有新数据流入 */
    uint64_t rx = usb_uac_get_rx_bytes();
    bool inflow = (rx > s_last_rx);
    s_last_rx = rx;
    int vol = audio_pipeline_get_volume();
    char d[96];
    snprintf(d, sizeof(d), "音量 %d%% | 已收 %.2f MB | %s",
             vol, (double)rx / (1024.0 * 1024.0), inflow ? "数据流入中" : "无数据");
    lv_label_set_text(s_lbl_diag, d);
}
