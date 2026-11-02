/**
 * FAKE_POD_NANO USB 模式选择页实现
 */

#include "ui_view_usbmode.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "usb_uac_service.h"

#include <stdio.h>

#define ACCENT      lv_palette_main(LV_PALETTE_AMBER)
#define CARD_BG     lv_color_make(20, 20, 26)
#define CARD_BORDER lv_color_make(44, 44, 54)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_msg = NULL;

static void home_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void dac_cb(lv_event_t *e)
{
    (void)e;
    if (usb_uac_service_start() == ESP_OK) {
        ui_switch_view(UI_VIEW_UAC);
    }
}

static void msc_cb(lv_event_t *e)
{
    (void)e;
    if (s_msg) {
        lv_label_set_text(s_msg, "U盘模式：需要独立 MSC 固件，本固件暂未启用");
        lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t* make_big_btn(lv_obj_t *parent, const char *title, const char *sub,
                              lv_event_cb_t cb, int y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 380, 92);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, CARD_BG, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, CARD_BORDER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = lv_label_create(btn);
    lv_obj_set_style_text_font(t, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 24, -12);

    lv_obj_t *s = lv_label_create(btn);
    lv_obj_set_style_text_font(s, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s, lv_color_make(150, 150, 165), 0);
    lv_label_set_text(s, sub);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 24, 16);

    lv_obj_t *arrow = lv_label_create(btn);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(arrow, ACCENT, 0);
    lv_label_set_text(arrow, ">");
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -22, 0);
    return btn;
}

lv_obj_t* ui_view_usbmode_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn_back);
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lb, ACCENT, 0);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &ui_font_cjk_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "USB 模式");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    make_big_btn(s_screen, "USB DAC 声卡", "把本机当作电脑 USB 声卡使用", dac_cb, 70);
    make_big_btn(s_screen, "U盘模式", "把 TF 卡当作电脑 U 盘（需 MSC 固件）", msc_cb, 180);

    lv_obj_t *hint = lv_label_create(s_screen);
    lv_obj_set_style_text_font(hint, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(130, 130, 145), 0);
    lv_obj_set_width(hint, 380);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "提示：USB 声卡会占用 USB 接口；插 U 盘模式需重启切换。");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 300);

    s_msg = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_msg, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_msg, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_width(s_msg, 380);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_msg, "");
    lv_obj_align(s_msg, LV_ALIGN_TOP_MID, 0, 352);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    return s_screen;
}

void ui_view_usbmode_update(void)
{
}
