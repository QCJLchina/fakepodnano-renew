/**
 * FAKE_POD_NANO 系统设置页实现
 */

#include "ui_view_settings.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "audio_pipeline.h"
#include "imu_service.h"
#include "dac_cs43131.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#define ACCENT          lv_palette_main(LV_PALETTE_AMBER)
#define CARD_BG         lv_color_make(20, 20, 26)
#define CARD_BORDER     lv_color_make(44, 44, 54)
#define TEXT_DIM        lv_color_make(150, 150, 165)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_about = NULL;
static lv_obj_t *s_eq = NULL;
static lv_obj_t *s_btn_bright[6];
static lv_obj_t *s_btn_auto[4];
static lv_obj_t *s_btn_eq[5];
static lv_obj_t *s_btn_imu = NULL;

static const char *kEqName[5] = { "关闭", "流行", "摇滚", "古典", "人声" };

static const int kBrightPct[6] = { 10, 30, 50, 70, 90, 100 };
static const int kAutoOffSec[4] = { 30, 60, 180, 0 };
static const char *kAutoOffText[4] = { "30秒", "1分钟", "3分钟", "从不" };

/* ---------------------------------------------------------------
 * 选中态风格
 * --------------------------------------------------------------- */
static void style_toggle(lv_obj_t *btn, bool selected)
{
    lv_obj_set_style_border_width(btn, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, selected ? ACCENT : CARD_BORDER, 0);
    lv_obj_set_style_bg_color(btn, selected ? lv_color_make(38, 32, 22) : CARD_BG, 0);
}

static void refresh_bright_styles(void)
{
    int cur = ui_get_backlight_pct();
    for (int i = 0; i < 6; i++) {
        if (s_btn_bright[i]) style_toggle(s_btn_bright[i], kBrightPct[i] == cur);
    }
}

static void refresh_auto_styles(void)
{
    int cur = ui_get_auto_off_sec();
    for (int i = 0; i < 4; i++) {
        if (s_btn_auto[i]) style_toggle(s_btn_auto[i], kAutoOffSec[i] == cur);
    }
}

/* ---------------------------------------------------------------
 * 事件
 * --------------------------------------------------------------- */
static void bright_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 5) return;
    ui_set_backlight_pct(kBrightPct[idx]);
    refresh_bright_styles();
}

static void auto_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 3) return;
    ui_set_auto_off_sec(kAutoOffSec[idx]);
    refresh_auto_styles();
}

static void home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void about_open_cb(lv_event_t *e)
{
    (void)e;
    if (s_about) lv_obj_clear_flag(s_about, LV_OBJ_FLAG_HIDDEN);
}

static void about_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_about) lv_obj_add_flag(s_about, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------
 * 均衡器
 * --------------------------------------------------------------- */
static void eq_refresh_styles(void)
{
    int cur = audio_pipeline_get_eq();
    for (int i = 0; i < 5; i++) {
        if (s_btn_eq[i]) style_toggle(s_btn_eq[i], i == cur);
    }
}

static void eq_open_cb(lv_event_t *e)
{
    (void)e;
    if (s_eq) {
        eq_refresh_styles();
        lv_obj_clear_flag(s_eq, LV_OBJ_FLAG_HIDDEN);
    }
}

static void eq_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_eq) lv_obj_add_flag(s_eq, LV_OBJ_FLAG_HIDDEN);
}

static void eq_pick_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 4) return;
    audio_pipeline_set_eq(idx);
    eq_refresh_styles();
}

/* 测试音：验证 DAC/耳放是否出声（不经过 USB）。
 * 内部会按 44.1k/16bit 初始化 DAC，并依次试播 5 个音质档。 */
static void tone_click_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = audio_pipeline_play_test_tone(1000, 800, 80);
    if (err != ESP_OK) {
        ESP_LOGW("SETTINGS", "测试音播放失败: %s", esp_err_to_name(err));
    }
}

/* ---------------------------------------------------------------
 * IMU 手势开关
 * --------------------------------------------------------------- */
static void imu_refresh_label(void)
{
    if (!s_btn_imu) return;
    lv_obj_t *lbl = lv_obj_get_child(s_btn_imu, 0);
    if (!lbl) return;
    if (!imu_service_present()) {
        lv_label_set_text(lbl, "IMU 未检测");
    } else {
        lv_label_set_text(lbl, imu_service_get_enabled() ? "IMU 手势 开" : "IMU 手势 关");
    }
}

static void imu_toggle_cb(lv_event_t *e)
{
    (void)e;
    if (!imu_service_present()) return;
    imu_service_set_enabled(!imu_service_get_enabled());
    imu_refresh_label();
}

/* ---------------------------------------------------------------
 * 创建按钮小工具
 * --------------------------------------------------------------- */
static lv_obj_t* make_btn(lv_obj_t *parent, const char *text, int w, int h,
                          lv_event_cb_t cb, int user_idx)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, CARD_BG, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, CARD_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(intptr_t)user_idx);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t* make_section_label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl, TEXT_DIM, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 16, y);
    return lbl;
}

/* ---------------------------------------------------------------
 * 关于本设备（覆盖层）
 * --------------------------------------------------------------- */
static void build_about(lv_obj_t *parent)
{
    s_about = lv_obj_create(parent);
    lv_obj_set_size(s_about, 460, 460);
    lv_obj_set_pos(s_about, 0, 0);
    lv_obj_set_style_bg_color(s_about, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_about, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_about, 0, 0);
    lv_obj_set_style_pad_all(s_about, 16, 0);

    lv_obj_t *btn_back = lv_btn_create(s_about);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, about_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b = lv_label_create(btn_back);
    lv_obj_set_style_text_font(lbl_b, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_b, ACCENT, 0);
    lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_b);

    lv_obj_t *title = lv_label_create(s_about);
    lv_obj_set_style_text_font(title, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "关于本设备");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *info = lv_label_create(s_about);
    lv_obj_set_style_text_font(info, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(info, lv_color_make(200, 200, 210), 0);
    lv_obj_set_style_text_line_space(info, 8, 0);
    lv_label_set_text(info,
        "型号：FAKE POD NANO\n"
        "设计：QCJL\n"
        "\n"
        "主控：ESP32-S3 双核 240MHz\n"
        "存储：16MB Flash / 8MB PSRAM\n"
        "解码：Cirrus Logic CS43131\n"
        "屏幕：2.0 英寸 AMOLED 460x460\n"
        "硬件上限：384kHz / 32bit & DSD256\n"
        "USB 声卡：最高 96kHz / 24bit");
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 10, 70);

    lv_obj_add_flag(s_about, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------
 * 均衡器覆盖层
 * --------------------------------------------------------------- */
static void build_eq(lv_obj_t *parent)
{
    s_eq = lv_obj_create(parent);
    lv_obj_set_size(s_eq, 460, 460);
    lv_obj_set_pos(s_eq, 0, 0);
    lv_obj_set_style_bg_color(s_eq, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_eq, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_eq, 0, 0);
    lv_obj_set_style_pad_all(s_eq, 16, 0);

    lv_obj_t *btn_back = lv_btn_create(s_eq);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, eq_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn_back);
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lb, ACCENT, 0);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(s_eq);
    lv_obj_set_style_text_font(title, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "均衡器");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    for (int i = 0; i < 5; i++) {
        s_btn_eq[i] = make_btn(s_eq, kEqName[i], 260, 54, eq_pick_cb, i);
        lv_obj_align(s_btn_eq[i], LV_ALIGN_TOP_MID, 0, 62 + i * 62);
    }

    /* 测试音：验证 DAC/耳放通路（内部依次试播 5 个音质档） */
    lv_obj_t *btn_tone = lv_btn_create(s_eq);
    lv_obj_set_size(btn_tone, 260, 46);
    lv_obj_align(btn_tone, LV_ALIGN_TOP_MID, 0, 62 + 5 * 62);
    lv_obj_set_style_radius(btn_tone, 12, 0);
    lv_obj_set_style_bg_color(btn_tone, lv_color_make(30, 24, 18), 0);
    lv_obj_set_style_border_width(btn_tone, 1, 0);
    lv_obj_set_style_border_color(btn_tone, ACCENT, 0);
    lv_obj_set_style_shadow_width(btn_tone, 0, 0);
    lv_obj_add_event_cb(btn_tone, tone_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_tone = lv_label_create(btn_tone);
    lv_obj_set_style_text_font(lbl_tone, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl_tone, ACCENT, 0);
    lv_label_set_text(lbl_tone, "测试音 1kHz");
    lv_obj_center(lbl_tone);

    lv_obj_add_flag(s_eq, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------
 * 创建页面
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_settings_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* 顶部：返回 + 标题 */
    lv_obj_t *btn_home = lv_btn_create(s_screen);
    lv_obj_set_size(btn_home, 60, 40);
    lv_obj_align(btn_home, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_radius(btn_home, 10, 0);
    lv_obj_set_style_bg_color(btn_home, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_home, 0, 0);
    lv_obj_add_event_cb(btn_home, home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_obj_set_style_text_font(lbl_home, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_home, ACCENT, 0);
    lv_label_set_text(lbl_home, LV_SYMBOL_HOME);
    lv_obj_center(lbl_home);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "系统设置");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* IMU 手势开关（右上角，可显示"未检测"） */
    s_btn_imu = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_imu, 132, 36);
    lv_obj_align(s_btn_imu, LV_ALIGN_TOP_RIGHT, -14, 14);
    lv_obj_set_style_radius(s_btn_imu, 10, 0);
    lv_obj_set_style_bg_color(s_btn_imu, CARD_BG, 0);
    lv_obj_set_style_border_width(s_btn_imu, 1, 0);
    lv_obj_set_style_border_color(s_btn_imu, CARD_BORDER, 0);
    lv_obj_set_style_shadow_width(s_btn_imu, 0, 0);
    lv_obj_add_event_cb(s_btn_imu, imu_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_imu = lv_label_create(s_btn_imu);
    lv_obj_set_style_text_font(lbl_imu, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl_imu, lv_color_white(), 0);
    lv_label_set_text(lbl_imu, "IMU 手势 开");
    lv_obj_center(lbl_imu);

    /* 屏幕亮度 6 档 */
    make_section_label(s_screen, "屏幕亮度", 60);
    const int bx[3] = { 14, 164, 314 };
    for (int i = 0; i < 6; i++) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", kBrightPct[i]);
        int row = i / 3, col = i % 3;
        s_btn_bright[i] = make_btn(s_screen, txt, 132, 54, bright_click_cb, i);
        lv_obj_set_pos(s_btn_bright[i], bx[col], 88 + row * 62);
    }

    /* 自动息屏 */
    make_section_label(s_screen, "自动息屏", 220);
    for (int i = 0; i < 4; i++) {
        s_btn_auto[i] = make_btn(s_screen, kAutoOffText[i], 100, 48, auto_click_cb, i);
        lv_obj_set_pos(s_btn_auto[i], 14 + i * 110, 246);
    }

    /* 音量上限（固定） */
    lv_obj_t *vol = lv_label_create(s_screen);
    lv_obj_set_style_text_font(vol, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(vol, TEXT_DIM, 0);
    lv_label_set_text(vol, "音量上限：85%（固定）");
    lv_obj_align(vol, LV_ALIGN_TOP_LEFT, 16, 312);

    /* 均衡器 + 关于本设备（并排） */
    lv_obj_t *btn_eq = lv_btn_create(s_screen);
    lv_obj_set_size(btn_eq, 210, 56);
    lv_obj_align(btn_eq, LV_ALIGN_TOP_LEFT, 14, 350);
    lv_obj_set_style_radius(btn_eq, 12, 0);
    lv_obj_set_style_bg_color(btn_eq, CARD_BG, 0);
    lv_obj_set_style_border_width(btn_eq, 1, 0);
    lv_obj_set_style_border_color(btn_eq, CARD_BORDER, 0);
    lv_obj_set_style_shadow_width(btn_eq, 0, 0);
    lv_obj_add_event_cb(btn_eq, eq_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_eq = lv_label_create(btn_eq);
    lv_obj_set_style_text_font(lbl_eq, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl_eq, lv_color_white(), 0);
    lv_label_set_text(lbl_eq, "均衡器  >");
    lv_obj_center(lbl_eq);

    lv_obj_t *btn_about = lv_btn_create(s_screen);
    lv_obj_set_size(btn_about, 210, 56);
    lv_obj_align(btn_about, LV_ALIGN_TOP_LEFT, 236, 350);
    lv_obj_set_style_radius(btn_about, 12, 0);
    lv_obj_set_style_bg_color(btn_about, CARD_BG, 0);
    lv_obj_set_style_border_width(btn_about, 1, 0);
    lv_obj_set_style_border_color(btn_about, CARD_BORDER, 0);
    lv_obj_set_style_shadow_width(btn_about, 0, 0);
    lv_obj_add_event_cb(btn_about, about_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_about = lv_label_create(btn_about);
    lv_obj_set_style_text_font(lbl_about, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl_about, lv_color_white(), 0);
    lv_label_set_text(lbl_about, "关于本设备  >");
    lv_obj_center(lbl_about);

    build_about(s_screen);
    build_eq(s_screen);

    refresh_bright_styles();
    refresh_auto_styles();
    imu_refresh_label();
    return s_screen;
}

void ui_view_settings_update(void)
{
    refresh_bright_styles();
    refresh_auto_styles();
    imu_refresh_label();
}
