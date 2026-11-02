/**
 * FAKE_POD_NANO 桌面主页（2x2 宫格大卡片）
 * ============================================
 * 视觉基准：prototype/index.html 的 .status-bar / .launcher-grid / .launcher-card
 *          → design_ref/launcher.png（460x460 设计截图，2x 渲染）。
 *
 * 版式（页面内边距 18 / 14，全部来自 ui_theme.h 的令牌，本文件不写死颜色和像素）：
 *   状态栏  (18,14) 424x24：左 %H:%M；右 CS43131(强调色) + VOL nn%(次级) + ⚡ nn%
 *   宫格    205x183 四张，间距 14，起点 (18,46)：
 *             (18,46) 正在播放 → 播放器页    (237,46) 音乐目录 → 目录页
 *             (18,243) USB DAC → USB 模式页  (237,243) 系统设置 → 设置页
 *   手势条  60x4 底部居中（ui_gesture_hint_create）
 *
 * 卡片内部（图标左上 / 文字左下）由通用控件 ui_launcher_card_create 统一绘制，
 * 本页只负责摆放与动态文案，不再自己重排卡片内容。
 *
 * 动态数据由 ui_manager 每 200ms 调一次 ui_view_home_update()：
 *   时钟（未校时 --:--）/ 音量（audio_pipeline_get_volume）/ 电量（battery_percent）
 *   / 卡片1 副标题（当前曲目名）。
 */

#include "ui_view_home.h"

#include "ui_manager.h"
#include "ui_theme.h"
#include "ui_view_files.h"

#include "audio_pipeline.h"
#include "battery_service.h"
#include "player_service.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------------------------------------------------------------
 * 页面内对象
 * --------------------------------------------------------------- */
static lv_obj_t *s_screen      = NULL;
static lv_obj_t *s_lbl_clock   = NULL;  /* 状态栏左：时间 */
static lv_obj_t *s_lbl_vol     = NULL;  /* 状态栏右：VOL nn% */
static lv_obj_t *s_lbl_bat     = NULL;  /* 状态栏右：nn%（⚡ 由状态栏控件画） */
static lv_obj_t *s_lbl_now     = NULL;  /* 卡片1 副标题：当前曲目 */
static lv_obj_t *s_lbl_touch   = NULL;  /* 触摸未就绪提示（次级灰小字） */

/* 副标题未播放时的默认文案（对应设计稿空状态） */
#define HOME_NOW_PLAYING_IDLE "点按开始播放"

/* ---------------------------------------------------------------
 * 卡片点击回调
 * --------------------------------------------------------------- */
static void card_playing_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_PLAYER);
}

static void card_files_cb(lv_event_t *e)
{
    (void)e;
    ui_view_files_enter();              /* 重置到 TF 卡根目录并刷新列表 */
    ui_switch_view(UI_VIEW_FILES);
}

static void card_uac_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI("HOME", "主页卡片 3 (USB DAC / 模式) 被点击 -> 切换至 USB 模式选择页");
    ui_switch_view(UI_VIEW_USB_MODE);   /* USB 模式：DAC 声卡 / U 盘 */
}

static void card_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_SETTINGS);
}

/* ---------------------------------------------------------------
 * 当前曲目 → 卡片副标题
 *
 * 设计稿卡片1 副标题只显示曲名（"加州旅馆"）。player_service 的
 * audio_track_info_t 目前只带格式/时长，不带标题/艺术家元数据，
 * 所以统一按常见命名 "标题 - 艺术家 (后缀)" 从文件名推：
 *   /sdcard/欧美发烧经典原盘/加州旅馆 - Eagles (Live).flac -> 加州旅馆
 * 没有分隔符则整个文件名（去扩展名）。
 * --------------------------------------------------------------- */
static void home_now_playing_text(char *out, size_t out_len, const player_status_t *st)
{
    if (!out || out_len < 2) return;
    out[0] = '\0';

    const char *path = st->current_path;
    const char *name = strrchr(path, '/');
    name = name ? (name + 1) : path;
    if (!name || !*name) return;

    /* 去掉扩展名 */
    size_t n = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) n = (size_t)(dot - name);
    if (n >= out_len) n = out_len - 1;
    if (n == 0) return;

    /* "标题 - 艺术家"：只取标题部分，与设计稿一致 */
    size_t len = n;
    for (size_t i = 0; i + 2 < n; i++) {
        if (name[i] == ' ' && name[i + 1] == '-' && name[i + 2] == ' ') {
            len = i;
            break;
        }
    }
    while (len > 0 && name[len - 1] == ' ') len--;
    if (len == 0) len = n;

    memcpy(out, name, len);
    out[len] = '\0';
}

/* ---------------------------------------------------------------
 * 创建桌面主页
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_home_create(void)
{
    s_screen = ui_screen_create();      /* 纯黑 460x460，内边距 0，绝对定位 */

    /* 顶部状态栏：(18,14) 424x24，左时间 / 右 CS43131 + VOL + ⚡ 电量 */
    ui_home_status_bar_create(s_screen, &s_lbl_clock, &s_lbl_vol, &s_lbl_bat);

    /* 2x2 宫格：205x183，间距 14，起点 (18,46) */
    ui_launcher_card_create(s_screen, UI_GRID_X, UI_GRID_Y,
                            &ui_icon_music,
                            UI_GRAD_PLAYING_A, UI_GRAD_PLAYING_B,
                            "正在播放", HOME_NOW_PLAYING_IDLE,
                            card_playing_cb, &s_lbl_now);

    ui_launcher_card_create(s_screen, UI_GRID_X + UI_CARD_W + UI_CARD_GAP, UI_GRID_Y,
                            &ui_icon_folder,
                            UI_GRAD_FILES_A, UI_GRAD_FILES_B,
                            "音乐目录", "TF卡 / 专辑 / 无损",
                            card_files_cb, NULL);

    ui_launcher_card_create(s_screen, UI_GRID_X, UI_GRID_Y + UI_CARD_H + UI_CARD_GAP,
                            &ui_icon_headset,
                            UI_GRAD_UAC_A, UI_GRAD_UAC_B,
                            "USB DAC", "最高 384kHz/32bit",
                            card_uac_cb, NULL);

    ui_launcher_card_create(s_screen, UI_GRID_X + UI_CARD_W + UI_CARD_GAP,
                            UI_GRID_Y + UI_CARD_H + UI_CARD_GAP,
                            &ui_icon_gear,
                            UI_GRAD_SETTINGS_A, UI_GRAD_SETTINGS_B,
                            "系统设置", "屏幕亮度 / 休眠 / 关于",
                            card_settings_cb, NULL);

    /* 卡片1 副标题会被动态改写（曲名），改成省略号收尾，防止长文件名溢出色块 */
    if (s_lbl_now) lv_label_set_long_mode(s_lbl_now, LV_LABEL_LONG_DOT);

    /* 底部手势提示条（60x4，居中，距底 6px） */
    ui_gesture_hint_create(s_screen);

    /* 触摸未就绪提示：底部一行次级灰小字（现场没有串口时也能一眼看出来） */
    s_lbl_touch = ui_label_create(s_screen, "触摸未就绪 / 仅显示", UI_FONT_CJK_S, UI_COL_TEXT3);
    lv_obj_set_width(s_lbl_touch, UI_SCR_W - UI_PAD_X * 2);
    lv_label_set_long_mode(s_lbl_touch, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_lbl_touch, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_touch, LV_ALIGN_BOTTOM_MID, 0, -(UI_HINT_H + 8));
    lv_obj_add_flag(s_lbl_touch, LV_OBJ_FLAG_HIDDEN);

    return s_screen;
}

/* ---------------------------------------------------------------
 * 周期刷新（ui_manager 每 200ms 调用一次）
 * --------------------------------------------------------------- */
void ui_view_home_update(void)
{
    if (!s_screen) return;

    /* 触摸状态：没就绪时显示底部灰字提示 */
    if (s_lbl_touch) {
        if (ui_touch_ok()) {
            lv_obj_add_flag(s_lbl_touch, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_lbl_touch, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 时钟：未校时（1970 那一档）显示 --:-- */
    if (s_lbl_clock) {
        time_t now = time(NULL);
        struct tm tm_info;
        char buf[8];
        if (now > 1600000000 && localtime_r(&now, &tm_info)) {
            strftime(buf, sizeof(buf), "%H:%M", &tm_info);
        } else {
            snprintf(buf, sizeof(buf), "--:--");
        }
        lv_label_set_text(s_lbl_clock, buf);
    }

    /* 音量：audio_pipeline 的真实音量，变化时才刷新 label */
    if (s_lbl_vol) {
        static int last_vol = -1;
        int vol = (int)audio_pipeline_get_volume();
        if (vol < 0) vol = 0;
        if (vol > 100) vol = 100;
        if (vol != last_vol) {
            char buf[16];
            last_vol = vol;
            snprintf(buf, sizeof(buf), "VOL %d%%", vol);
            lv_label_set_text(s_lbl_vol, buf);
        }
    }

    /* 电量：ADC 读取有开销，首次立即读一次、之后每 5 秒一次（25 x 200ms） */
    if (s_lbl_bat) {
        static int bat_tick = 25;
        static int last_bat = -2;
        if (++bat_tick >= 25) {
            bat_tick = 0;
            int pct = battery_percent();
            if (pct < 0 || pct > 100) pct = -1;
            if (pct != last_bat) {
                char buf[16];
                last_bat = pct;
                if (pct >= 0) {
                    snprintf(buf, sizeof(buf), "%d%%", pct);   /* ⚡ 符号由状态栏控件画 */
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                lv_label_set_text(s_lbl_bat, buf);
            }
        }
    }

    /* 卡片1 副标题：有当前曲目就显示曲名，完全没曲目显示"点按开始播放" */
    if (s_lbl_now) {
        static char last[80] = { 0 };
        char title[80];
        player_status_t st;

        player_get_status(&st);
        if (st.state == PLAYER_STATE_STOPPED || st.state == PLAYER_STATE_ERROR ||
            !st.current_path[0]) {
            snprintf(title, sizeof(title), "%s", HOME_NOW_PLAYING_IDLE);
        } else {
            home_now_playing_text(title, sizeof(title), &st);
            if (!title[0]) snprintf(title, sizeof(title), "%s", HOME_NOW_PLAYING_IDLE);
        }

        if (strcmp(title, last) != 0) {
            snprintf(last, sizeof(last), "%s", title);
            lv_label_set_text(s_lbl_now, title);
        }
    }
}
