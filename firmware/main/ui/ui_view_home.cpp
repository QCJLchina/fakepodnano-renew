/**
 * FAKE_POD_NANO 桌面主页（2x2 宫格大触控卡片）
 * ============================================
 * 依据 UI_DESIGN_SPEC.md 第三节：
 *   卡片1 正在播放  -> 本地播放器页 (UI_VIEW_PLAYER)
 *   卡片2 音乐目录  -> 暂未实现，弹提示
 *   卡片3 USB DAC  -> USB 声卡页 (UI_VIEW_UAC)
 *   卡片4 系统设置  -> 暂未实现，弹提示
 * 开机默认停在此页。
 *
 * 中文显示依赖 ui_fonts.h 里的自定义 CJK 字体（20px 标题 / 16px 正文）。
 */

#include "ui_view_home.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "ui_view_files.h"
#include "player_service.h"
#include "usb_uac_service.h"
#include "battery_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static lv_obj_t *s_screen_home = NULL;
static lv_obj_t *s_lbl_clock = NULL;      /* 顶部时间 */
static lv_obj_t *s_lbl_bat = NULL;        /* 顶部电量 */
static lv_obj_t *s_lbl_now = NULL;        /* 卡片1副标题：当前曲目 */
static lv_obj_t *s_lbl_toast = NULL;      /* 底部临时提示 */
static lv_obj_t *s_lbl_touch_warn = NULL; /* 触摸未就绪提示 */
static lv_timer_t *s_toast_timer = NULL;

/* ---------------------------------------------------------------
 * 底部临时提示（2 秒后自动消失）
 * --------------------------------------------------------------- */
static void toast_timeout_cb(lv_timer_t *t)
{
    if (s_lbl_toast) lv_label_set_text(s_lbl_toast, " ");
    lv_timer_pause(t);
}

static void show_toast(const char *msg)
{
    if (!s_lbl_toast) return;
    lv_label_set_text(s_lbl_toast, msg);
    if (!s_toast_timer) {
        s_toast_timer = lv_timer_create(toast_timeout_cb, 2000, NULL);
    } else {
        lv_timer_reset(s_toast_timer);
        lv_timer_resume(s_toast_timer);
    }
}

/* ---------------------------------------------------------------
 * 卡片点击回调
 * --------------------------------------------------------------- */
static void card_now_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_PLAYER);
}

static void card_dir_click_cb(lv_event_t *e)
{
    (void)e;
    ui_view_files_enter();          /* 重置到 TF 卡根目录并刷新列表 */
    ui_switch_view(UI_VIEW_FILES);
}

static void card_dac_click_cb(lv_event_t *e)
{
    (void)e;
    /* 进入 USB 模式选择页：DAC 声卡 / U盘模式 */
    ui_switch_view(UI_VIEW_USB_MODE);
}

static void card_set_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_SETTINGS);
}

/* ---------------------------------------------------------------
 * 创建一张宫格卡片（图标 + 标题 + 副标题，纵向居中）
 * 返回卡片对象；副标题是最后一个子对象，可用 lv_obj_get_child(card, -1) 取到
 * --------------------------------------------------------------- */
static lv_obj_t* make_card(lv_obj_t *parent, const char *icon, const char *title,
                           const char *sub, lv_event_cb_t cb)
{
    lv_obj_t *card = lv_btn_create(parent);
    lv_obj_set_size(card, 190, 170);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_make(18, 18, 23), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_make(32, 27, 20), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_make(72, 54, 26), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ico = lv_label_create(card);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(ico, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(ico, icon);

    lv_obj_t *t = lv_label_create(card);
    lv_obj_set_style_text_font(t, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, title);

    lv_obj_t *s = lv_label_create(card);
    lv_obj_set_style_text_font(s, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s, lv_color_make(150, 150, 165), 0);
    lv_obj_set_style_max_width(s, 170, 0);
    lv_label_set_text(s, sub);

    return card;
}

/* ---------------------------------------------------------------
 * 创建桌面主页
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_home_create(void)
{
    s_screen_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_home, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_screen_home, 0, 0);

    /* 顶部状态栏：时间 + 硬件标识 */
    s_lbl_clock = lv_label_create(s_screen_home);
    lv_obj_set_style_text_font(s_lbl_clock, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_clock, lv_color_white(), 0);
    lv_label_set_text(s_lbl_clock, "--:--");
    lv_obj_align(s_lbl_clock, LV_ALIGN_TOP_LEFT, 20, 8);

    lv_obj_t *hw = lv_label_create(s_screen_home);
    lv_obj_set_style_text_font(hw, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hw, lv_color_make(140, 140, 155), 0);
    lv_label_set_text(hw, "CS43131 Hi-Fi");
    lv_obj_align(hw, LV_ALIGN_TOP_MID, 0, 10);

    /* 电量显示（右上角） */
    s_lbl_bat = lv_label_create(s_screen_home);
    lv_obj_set_style_text_font(s_lbl_bat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_bat, lv_color_make(140, 140, 155), 0);
    lv_label_set_text(s_lbl_bat, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_align(s_lbl_bat, LV_ALIGN_TOP_RIGHT, -20, 10);

    /* 2x2 宫格卡片 */
    lv_obj_t *c1 = make_card(s_screen_home, LV_SYMBOL_PLAY, "正在播放", "点按进入播放器", card_now_click_cb);
    lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 26, 38);
    s_lbl_now = lv_obj_get_child(c1, -1);

    lv_obj_t *c2 = make_card(s_screen_home, LV_SYMBOL_DIRECTORY, "音乐目录", "TF 卡 / 文件夹", card_dir_click_cb);
    lv_obj_align(c2, LV_ALIGN_TOP_RIGHT, -26, 38);

    lv_obj_t *c3 = make_card(s_screen_home, LV_SYMBOL_USB, "USB 模式", "DAC / U盘", card_dac_click_cb);
    lv_obj_align(c3, LV_ALIGN_BOTTOM_LEFT, 26, -66);

    lv_obj_t *c4 = make_card(s_screen_home, LV_SYMBOL_SETTINGS, "系统设置", "亮度 / 休眠", card_set_click_cb);
    lv_obj_align(c4, LV_ALIGN_BOTTOM_RIGHT, -26, -66);

    /* 底部手势提示条 + 临时提示文字 */
    lv_obj_t *hint = lv_obj_create(s_screen_home);
    lv_obj_set_size(hint, 120, 4);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_radius(hint, 2, 0);
    lv_obj_set_style_bg_color(hint, lv_color_make(80, 80, 95), 0);
    lv_obj_set_style_border_width(hint, 0, 0);

    s_lbl_toast = lv_label_create(s_screen_home);
    lv_obj_set_style_text_font(s_lbl_toast, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_lbl_toast, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(s_lbl_toast, " ");
    lv_obj_align(s_lbl_toast, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* 触摸初始化失败时的可见提示（无串口时也能看出问题） */
    s_lbl_touch_warn = lv_label_create(s_screen_home);
    lv_obj_set_style_text_font(s_lbl_touch_warn, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_lbl_touch_warn, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(s_lbl_touch_warn, "触摸未就绪");
    lv_obj_align(s_lbl_touch_warn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_flag(s_lbl_touch_warn, LV_OBJ_FLAG_HIDDEN);

    return s_screen_home;
}

/* ---------------------------------------------------------------
 * 周期刷新：时钟 + 卡片1当前曲目
 * --------------------------------------------------------------- */
void ui_view_home_update(void)
{
    if (!s_screen_home) return;

    /* 触摸未就绪时在底部显示红字提示 */
    if (s_lbl_touch_warn) {
        if (ui_touch_ok()) {
            lv_obj_add_flag(s_lbl_touch_warn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_lbl_touch_warn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 时钟：时间未校准时显示 --:-- */
    time_t now = time(NULL);
    struct tm tm_info;
    if (now > 1600000000 && localtime_r(&now, &tm_info)) {
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &tm_info);
        lv_label_set_text(s_lbl_clock, buf);
    } else {
        lv_label_set_text(s_lbl_clock, "--:--");
    }

    /* 电量：约每 5 秒刷新一次（避免频繁 ADC 读取） */
    static int bat_tick = 0;
    if (s_lbl_bat && (++bat_tick >= 25)) {
        bat_tick = 0;
        int p = battery_percent();
        char bb[32];
        if (p >= 0) {
            const char *sym = (p > 70) ? LV_SYMBOL_BATTERY_FULL :
                              (p > 35) ? LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_1;
            snprintf(bb, sizeof(bb), "%s %d%%", sym, p);
        } else {
            snprintf(bb, sizeof(bb), "%s --", LV_SYMBOL_BATTERY_EMPTY);
        }
        lv_label_set_text(s_lbl_bat, bb);
    }

    /* 卡片1 副标题：有正在播放/暂停的曲目就显示文件名 */
    if (s_lbl_now) {
        player_status_t st;
        player_get_status(&st);
        if (st.state != PLAYER_STATE_STOPPED && st.current_path[0]) {
            const char *fname = strrchr(st.current_path, '/');
            fname = fname ? (fname + 1) : st.current_path;
            lv_label_set_text(s_lbl_now, fname);
        } else {
            lv_label_set_text(s_lbl_now, "点按进入播放器");
        }
    }
}
