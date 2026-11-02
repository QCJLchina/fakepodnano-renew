/**
 * FAKE_POD_NANO 系统设置页 / 关于本设备页实现
 * ================================================
 * 视觉基准：prototype/index.html 的 `.settings-list / .setting-card / .setting-header /
 *           .setting-title / .setting-val / .setting-pills-row / .setting-option-btn`
 *           → design_ref/settings.png、design_ref/about.png（460x460 设计截图，2x）。
 *
 * 版式（页面内边距 18/14，子对象坐标从页面内容区起算，内容宽 424）：
 *   导航头   屏幕 y=14，高 36：左「桌面」药丸（home 图标 + 文案）+ 标题「系统设置」
 *   滚动列   屏幕 y=58 起，424x388，行间距 12
 *   设置卡片 424 宽、圆角 18、#18191D 底、1px 描边，内边距 16/14：
 *              卡片头（左标题白 / 右当前值强调色）+ 档位药丸行（高 32、圆角 10）
 *   手势条   底部居中（ui_gesture_hint_create）
 *
 * 行为保持与旧实现一致：
 *   - 屏幕亮度 6 档（10/30/50/70/90/100）真实调用 ui_set_backlight_pct()
 *   - 自动息屏 4 档（30秒/1分钟/3分钟/常亮）真实调用 ui_set_auto_off_sec()
 *   - 均衡器 5 预设（关闭/流行/摇滚/古典/人声）走 audio_pipeline_set_eq()
 *   - IMU 手势开关走 imu_service_set_enabled()（未检测到芯片时只显示，不可切换）
 *   - 防爆耳音量上限固定 85%（仅展示，旧实现即无对应设置接口）
 *   - 系统时钟同步：一键对时沿用 ble_time_sync 入口（BLE 服务本来开机即启动）
 *   - 关于本设备：独立 screen，导出 ui_view_settings_about_create/update()
 */

#include "ui_view_settings.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "audio_pipeline.h"
#include "imu_service.h"
#include "ble_time_sync.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdint.h>

/* ---------------------------------------------------------------
 * 版式常量（页面内容区坐标）
 * --------------------------------------------------------------- */
#define CARD_W          424
#define CARD_RADIUS     18
#define CARD_PAD_X      16
#define CARD_PAD_Y      14
#define PILL_H          32
#define HEAD_Y          14
#define PILL_Y          (HEAD_Y + 30)   /* 44：卡片头(20) + 间距(10) */
#define COL_X           0               /* 页面内容区左边 = 屏幕 18 */
#define COL_Y           44              /* 屏幕 58 */
#define COL_W           424
#define COL_H           388
#define COL_GAP         12

static const int  kBrightPct[6]   = { 10, 30, 50, 70, 90, 100 };
static const int  kAutoOffSec[4]  = { 30, 60, 180, 0 };
static const char *kAutoOffText[4] = { "30秒", "1分钟", "3分钟", "常亮" };
static const char *kEqName[5]     = { "关闭", "流行", "摇滚", "古典", "人声" };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_about  = NULL;

static lv_obj_t *s_val_bright = NULL;
static lv_obj_t *s_val_eq     = NULL;
static lv_obj_t *s_val_auto   = NULL;
static lv_obj_t *s_val_imu    = NULL;

static lv_obj_t *s_btn_bright[6];
static lv_obj_t *s_btn_auto[4];
static lv_obj_t *s_btn_eq[5];
static lv_obj_t *s_btn_imu[2];

/* ---------------------------------------------------------------
 * 基础小工具
 * --------------------------------------------------------------- */
static lv_obj_t* card_begin(lv_obj_t *col, int h)
{
    return ui_surface_create(col, 0, 0, CARD_W, h, CARD_RADIUS);
}

/** 卡片头：左标题（白）+ 右当前值（强调色），返回当前值 label 供刷新 */
static lv_obj_t* card_header(lv_obj_t *card, const char *title)
{
    lv_obj_t *t = ui_label_create(card, title, UI_FONT_CJK_S, UI_COL_TEXT);
    lv_obj_set_width(t, 250);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, CARD_PAD_X, HEAD_Y);

    lv_obj_t *v = ui_label_create(card, "", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_set_width(v, 150);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -CARD_PAD_X, HEAD_Y);
    return v;
}

/** 一行说明文字（次级灰，单行省略） */
static lv_obj_t* note_line(lv_obj_t *card, const char *txt, int y)
{
    lv_obj_t *l = ui_label_create(card, txt, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(l, CARD_W - CARD_PAD_X * 2);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, CARD_PAD_X, y);
    return l;
}

/** 档位药丸行：n 个等宽药丸，gap 6（6 档）/ 8（其余），注册带索引的回调 */
static void build_pill_row(lv_obj_t *card, const char *const *names, int n,
                           lv_event_cb_t cb, lv_obj_t **out)
{
    const int gap = (n == 6) ? 6 : 8;
    const int w = (CARD_W - CARD_PAD_X * 2 - (n - 1) * gap) / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *p = ui_option_pill_create(card, CARD_PAD_X + i * (w + gap), PILL_Y,
                                            w, PILL_H, names[i], false, NULL);
        lv_obj_add_event_cb(p, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        out[i] = p;
    }
}

/** 档位选中态：先走皮肤 helper，再补 bg_opa。
 *  ui_option_pill_create 内部 strip_obj() 会清掉主题默认样式，导致 bg_opa = 0；
 *  只设 bg_color 时高亮档位会变成纯黑（黑字看不见）。这里统一补不透明度。 */
static void pill_set(lv_obj_t *p, bool on)
{
    if (!p) return;
    ui_option_pill_set_active(p, on);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
}

/** 1px 分隔线（用设计令牌，不手写颜色） */
static void hairline(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, w, 1);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_bg_color(l, UI_COL_SURFACE_LINE, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---------------------------------------------------------------
 * 刷新选中态 / 当前值
 * --------------------------------------------------------------- */
static void refresh_bright(void)
{
    int cur = ui_get_backlight_pct();
    for (int i = 0; i < 6; i++) {
        if (s_btn_bright[i]) pill_set(s_btn_bright[i], kBrightPct[i] == cur);
    }
    if (s_val_bright) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", cur);
        lv_label_set_text(s_val_bright, buf);
    }
}

static void refresh_auto(void)
{
    int cur = ui_get_auto_off_sec();
    int hit = -1;
    for (int i = 0; i < 4; i++) {
        bool on = (kAutoOffSec[i] == cur);
        if (on) hit = i;
        if (s_btn_auto[i]) pill_set(s_btn_auto[i], on);
    }
    if (s_val_auto) {
        if (hit >= 0) {
            lv_label_set_text(s_val_auto, kAutoOffText[hit]);
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "常亮");
            lv_label_set_text(s_val_auto, buf);
        }
    }
}

static void refresh_eq(void)
{
    int cur = audio_pipeline_get_eq();
    for (int i = 0; i < 5; i++) {
        if (s_btn_eq[i]) pill_set(s_btn_eq[i], i == cur);
    }
    if (s_val_eq) {
        lv_label_set_text(s_val_eq, (cur >= 0 && cur < 5) ? kEqName[cur] : "关闭");
    }
}

static void refresh_imu(void)
{
    bool present = imu_service_present();
    bool on = present && imu_service_get_enabled();
    if (s_btn_imu[0]) pill_set(s_btn_imu[0], on);
    if (s_btn_imu[1]) pill_set(s_btn_imu[1], present && !on);
    if (s_val_imu) lv_label_set_text(s_val_imu, present ? (on ? "开" : "关") : "未检测");
}

/* ---------------------------------------------------------------
 * 事件
 * --------------------------------------------------------------- */
static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void bright_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 5) return;
    ui_set_backlight_pct(kBrightPct[idx]);
    refresh_bright();
}

static void auto_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 3) return;
    ui_set_auto_off_sec(kAutoOffSec[idx]);
    refresh_auto();
}

static void eq_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > 4) return;
    audio_pipeline_set_eq(idx);
    refresh_eq();
}

/** 测试音：验证 DAC/耳放通路（不经过 USB），沿用旧的 audio_pipeline 接口 */
static void tone_click_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = audio_pipeline_play_test_tone(1000, 800, 80);
    if (err != ESP_OK) {
        ESP_LOGW("SETTINGS", "测试音播放失败: %s", esp_err_to_name(err));
    }
}

static void imu_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!imu_service_present()) return;
    imu_service_set_enabled(idx == 0);
    refresh_imu();
}

/** 一键对时：沿用 ble_time_sync 入口（服务开机已启动，这里再次广播校时） */
static void sync_click_cb(lv_event_t *e)
{
    (void)e;
#if defined(ESP_PLATFORM)
    esp_err_t err = ble_time_sync_start();
    if (err != ESP_OK) {
        ESP_LOGW("SETTINGS", "BLE 对时启动失败: %s", esp_err_to_name(err));
    }
#endif
}

static void about_open_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_t *about = ui_view_settings_about_create();
    if (about) lv_scr_load(about);
}

/* ---------------------------------------------------------------
 * 关于本设备（独立 screen）
 * --------------------------------------------------------------- */
static void about_back_cb(lv_event_t *e)
{
    (void)e;
    if (s_screen) lv_scr_load(s_screen);
}

static void build_about_page(lv_obj_t *screen)
{
    lv_obj_t *page = ui_page_create(screen);
    ui_nav_header_create(page, 14, "设置", "关于本设备", about_back_cb, NULL);

    lv_obj_t *col = ui_scroll_column_create(page, COL_X, COL_Y, COL_W, COL_H, COL_GAP);

    /* 铭牌区：渐变底 + 强调色描边，大标题 + 软件设计 */
    lv_obj_t *name = ui_surface_create(col, 0, 0, CARD_W, 96, CARD_RADIUS);
    lv_obj_set_style_bg_color(name, UI_COL_CARD_TOP, 0);
    lv_obj_set_style_bg_grad_color(name, UI_COL_CARD_BOT, 0);
    lv_obj_set_style_bg_grad_dir(name, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(name, UI_COL_ACCENT_LINE, 0);

    lv_obj_t *title = ui_label_create(name, "FAKE POD NANO", UI_FONT_LG, UI_COL_TEXT);
    lv_obj_set_width(title, CARD_W - 32);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *design = ui_label_create(name, "软件设计：QCJL", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_set_width(design, CARD_W - 32);
    lv_label_set_long_mode(design, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(design, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(design, LV_ALIGN_TOP_MID, 0, 56);

    /* 状态信息卡片 */
    lv_obj_t *info = ui_surface_create(col, 0, 0, CARD_W, 236, CARD_RADIUS);
    lv_obj_t *head = ui_label_create(info, "状态信息", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_set_width(head, 200);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(head, CARD_PAD_X, 12);

    static const char *rows_l[5] = {
        "主控芯片", "存储规格", "解码芯片", "显示面板", "硬件最高规格"
    };
    static const char *rows_v[5] = {
        "ESP32-S3 Dual-Core 240MHz",
        "16MB Flash / 8MB Octal PSRAM",
        "Cirrus Logic CS43131 MasterHIFI",
        "2.0\" AMOLED 460x460 (CO5300)",
        "384.0 kHz / 32-bit & DSD256",
    };

    const int y0 = 42;
    const int rh = 36;
    for (int i = 0; i < 5; i++) {
        if (i > 0) hairline(info, CARD_PAD_X, y0 + i * rh - 1, CARD_W - CARD_PAD_X * 2);

        lv_obj_t *lab = ui_label_create(info, rows_l[i], UI_FONT_CJK_S, UI_COL_TEXT2);
        lv_obj_set_width(lab, 110);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(lab, 20, y0 + i * rh + 8);

        lv_color_t vc = (i == 4) ? UI_COL_ACCENT : UI_COL_TEXT;
        lv_obj_t *val = ui_label_create(info, rows_v[i], UI_FONT_SM, vc);
        lv_obj_set_width(val, 272);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(val, LV_ALIGN_TOP_RIGHT, -20, y0 + i * rh + 9);
    }

    ui_gesture_hint_create(screen);
}

lv_obj_t* ui_view_settings_about_create(void)
{
    if (s_about) return s_about;
    s_about = ui_screen_create();
    build_about_page(s_about);
    return s_about;
}

void ui_view_settings_about_update(void)
{
    /* 纯静态信息，无需周期刷新 */
}

/* ---------------------------------------------------------------
 * 创建设置页
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_settings_create(void)
{
    s_screen = ui_screen_create();
    lv_obj_t *page = ui_page_create(s_screen);

    /* 导航头：屏幕 y=14 */
    ui_nav_header_create(page, 14, "桌面", "系统设置", back_home_cb, NULL);

    /* 滚动列：屏幕 y=58，424x388，行间距 12 */
    lv_obj_t *col = ui_scroll_column_create(page, COL_X, COL_Y, COL_W, COL_H, COL_GAP);

    /* ① 屏幕亮度（固定 6 档，点击即时生效） */
    {
        lv_obj_t *c = card_begin(col, 90);
        s_val_bright = card_header(c, "屏幕亮度 (固定6档)");
        static const char *names[6] = { "10%", "30%", "50%", "70%", "90%", "100%" };
        build_pill_row(c, names, 6, bright_click_cb, s_btn_bright);
    }

    /* ② 均衡器（5 预设）+ 测试音 */
    {
        lv_obj_t *c = card_begin(col, 116);
        s_val_eq = card_header(c, "均衡器");
        build_pill_row(c, kEqName, 5, eq_click_cb, s_btn_eq);

        lv_obj_t *tone = ui_label_create(c, "测试音 1kHz", UI_FONT_CJK_S, UI_COL_TEXT3);
        lv_obj_set_width(tone, 220);
        lv_label_set_long_mode(tone, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(tone, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(tone, LV_ALIGN_BOTTOM_RIGHT, -CARD_PAD_X, -10);
        lv_obj_add_flag(tone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tone, tone_click_cb, LV_EVENT_CLICKED, NULL);
    }

    /* ③ 自动息屏时间（4 档） */
    {
        lv_obj_t *c = card_begin(col, 90);
        s_val_auto = card_header(c, "自动息屏时间");
        build_pill_row(c, kAutoOffText, 4, auto_click_cb, s_btn_auto);
    }

    /* ④ 手势感应 IMU（开 / 关 + 一行说明） */
    {
        lv_obj_t *c = card_begin(col, 114);
        s_val_imu = card_header(c, "手势感应 IMU");
        static const char *names[2] = { "开", "关" };
        build_pill_row(c, names, 2, imu_click_cb, s_btn_imu);
        note_line(c, "手势切歌 / 音量 / 播放暂停；关闭后仅保留芯片检测", 82);
    }

    /* ⑤ 防爆耳高音量限额（固定 85%） */
    {
        lv_obj_t *c = card_begin(col, 74);
        lv_obj_t *v = card_header(c, "防爆耳高音量限额");
        lv_label_set_text(v, "85% 锁");
        note_line(c, "限制单档手势最高调节至 85%，保护听力与耳塞单元", 42);
    }

    /* ⑥ 系统时钟同步（三行说明 + 一键对时） */
    {
        lv_obj_t *c = card_begin(col, 126);
        lv_obj_t *t = ui_label_create(c, "系统时钟同步", UI_FONT_CJK_S, UI_COL_TEXT);
        lv_obj_set_width(t, 240);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, CARD_PAD_X, HEAD_Y);

        lv_obj_t *pill = ui_pill_create(c, 0, 0, 96, 28,
                                        UI_COL_ACCENT_SOFT, UI_COL_ACCENT_LINE);
        lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -CARD_PAD_X, 12);
        lv_obj_t *plbl = ui_label_create(pill, "一键对时", UI_FONT_CJK_S, UI_COL_ACCENT);
        lv_obj_center(plbl);
        lv_obj_add_event_cb(pill, sync_click_cb, LV_EVENT_CLICKED, NULL);

        note_line(c, "方式 1: 手机蓝牙配对自动同步 (Apple CTS / Android)", 48);
        note_line(c, "方式 2: Type-C 连接电脑 USB 声卡时静默校准", 68);
        note_line(c, "方式 3: 内部 RTC 超低功耗走时（息屏不丢时间）", 88);
    }

    /* ⑦ 关于（整行可点） */
    {
        lv_obj_t *c = card_begin(col, 56);
        lv_obj_t *t = ui_label_create(c, "关于", UI_FONT_CJK_S, UI_COL_TEXT);
        lv_obj_set_width(t, 200);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, CARD_PAD_X, 0);

        lv_obj_t *arrow = ui_label_create(c, LV_SYMBOL_RIGHT, UI_FONT_SM, UI_COL_TEXT3);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -CARD_PAD_X, 0);

        lv_obj_t *model = ui_label_create(c, "FAKE POD NANO", UI_FONT_SM, UI_COL_TEXT2);
        lv_obj_set_width(model, 200);
        lv_label_set_long_mode(model, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(model, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align_to(model, arrow, LV_ALIGN_OUT_LEFT_MID, -8, 0);

        ui_press_feedback(c);
        lv_obj_add_event_cb(c, about_open_cb, LV_EVENT_CLICKED, NULL);
    }

    ui_gesture_hint_create(s_screen);

    /* 首次同步当前状态（sim 假数据：背光 70%、自动息屏 30s、IMU 开、EQ=0） */
    refresh_bright();
    refresh_auto();
    refresh_eq();
    refresh_imu();

    return s_screen;
}

void ui_view_settings_update(void)
{
    refresh_bright();
    refresh_auto();
    refresh_eq();
    refresh_imu();
}
