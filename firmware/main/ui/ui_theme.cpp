/**
 * FAKE_POD_NANO UI 设计令牌与通用控件实现
 * ==========================================
 * 视觉基准：prototype/index.html（460x460 CSS 原型）→ design_ref 目录下的 460x460 截图。
 * 这里所有数值都能在原型 CSS 里找到对应行，改 UI 请改这里，不要在页面里写死。
 *
 * 性能约定（ESP32-S3 @ 460x460 QSPI）：
 *   - 不使用 box-shadow 光晕（整帧重绘代价高，纯黑背景上视觉收益也小）；
 *   - 圆角卡片用 LVGL 原生 round 绘制，不用图片（省 flash / 省 PSRAM）；
 *   - 图标是 TRUE_COLOR_ALPHA 的 RGB565 位图，白色字形靠 img_recolor 上色。
 */

#include "ui_theme.h"

#include <stdio.h>

/* ------------------------------------------------------------------
 * 小工具
 * ------------------------------------------------------------------ */
static void strip_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

/** 透明容器（不带任何默认样式与内边距） */
static lv_obj_t* transparent_box(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    strip_obj(box);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    return box;
}

/* ------------------------------------------------------------------
 * 页面骨架
 * ------------------------------------------------------------------ */
lv_obj_t* ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, UI_SCR_W, UI_SCR_H);
    lv_obj_set_style_bg_color(scr, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    return scr;
}

lv_obj_t* ui_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    strip_obj(page);
    lv_obj_set_size(page, UI_SCR_W, UI_SCR_H);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_left(page, UI_PAD_X, 0);
    lv_obj_set_style_pad_right(page, UI_PAD_X, 0);
    lv_obj_set_style_pad_top(page, UI_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(page, UI_PAD_Y, 0);
    return page;
}

lv_obj_t* ui_gesture_hint_create(lv_obj_t *parent)
{
    lv_obj_t *hint = lv_obj_create(parent);
    strip_obj(hint);
    lv_obj_set_size(hint, UI_HINT_W, UI_HINT_H);
    lv_obj_set_style_bg_color(hint, lv_color_hex(0x3F4045), 0);
    lv_obj_set_style_bg_opa(hint, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hint, UI_HINT_H / 2, 0);
    lv_obj_set_style_border_width(hint, 0, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    return hint;
}

/* ------------------------------------------------------------------
 * 基础控件
 * ------------------------------------------------------------------ */
lv_obj_t* ui_label_create(lv_obj_t *parent, const char *txt,
                          const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, 0, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl, txt ? txt : "");
    return lbl;
}

void ui_label_align_right(lv_obj_t *lbl)
{
    if (!lbl) return;
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
}

void ui_press_feedback(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(obj, UI_COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, UI_COL_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 1, LV_STATE_PRESSED);
}

lv_obj_t* ui_card_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    strip_obj(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, UI_COL_CARD_TOP, 0);
    lv_obj_set_style_bg_grad_color(card, UI_COL_CARD_BOT, 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, UI_COL_CARD_LINE, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    ui_press_feedback(card);
    return card;
}

lv_obj_t* ui_surface_create(lv_obj_t *parent, int x, int y, int w, int h, int radius)
{
    lv_obj_t *box = lv_obj_create(parent);
    strip_obj(box);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, UI_COL_SURFACE_LINE, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    return box;
}

lv_obj_t* ui_pill_create(lv_obj_t *parent, int x, int y, int w, int h,
                         lv_color_t bg, lv_color_t border)
{
    lv_obj_t *pill = lv_obj_create(parent);
    strip_obj(pill);
    lv_obj_set_size(pill, w, h);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_style_bg_color(pill, bg, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, h / 2, 0);
    /* 描边固定 1px：与底色同色时视觉上不可见，避免调用方再传开关 */
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, border, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    ui_press_feedback(pill);
    return pill;
}

lv_obj_t* ui_scroll_column_create(lv_obj_t *parent, int x, int y, int w, int h, int gap)
{
    lv_obj_t *col = transparent_box(parent);
    lv_obj_set_size(col, w, h);
    lv_obj_set_pos(col, x, y);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, gap, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(col, w, LV_PART_SCROLLBAR);
    return col;
}

/* ------------------------------------------------------------------
 * 图标
 * ------------------------------------------------------------------ */
lv_obj_t* ui_icon_create(lv_obj_t *parent, int x, int y,
                         const lv_img_dsc_t *icon, lv_color_t color)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, icon);
    lv_obj_set_pos(img, x, y);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}

lv_obj_t* ui_icon_tile_create(lv_obj_t *parent, int x, int y, int size, int radius,
                              const lv_img_dsc_t *icon, lv_color_t c1, lv_color_t c2)
{
    lv_obj_t *tile = lv_obj_create(parent);
    strip_obj(tile);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, c1, 0);
    lv_obj_set_style_bg_grad_color(tile, c2, 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_HOR, 0); /* 原型 135deg */
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, radius, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *img = lv_img_create(tile);
    lv_img_set_src(img, icon);
    lv_obj_set_style_img_recolor(img, UI_COL_TEXT, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    return tile;
}

/* ------------------------------------------------------------------
 * 复合控件
 * ------------------------------------------------------------------ */
lv_obj_t* ui_launcher_card_create(lv_obj_t *parent, int x, int y,
                                  const lv_img_dsc_t *icon,
                                  lv_color_t grad_a, lv_color_t grad_b,
                                  const char *title, const char *desc,
                                  lv_event_cb_t on_click, lv_obj_t **out_desc)
{
    lv_obj_t *card = ui_card_create(parent, x, y, UI_CARD_W, UI_CARD_H);

    /* 左上：44x44 渐变图标底板（内边距 16） */
    ui_icon_tile_create(card, UI_CARD_PAD, UI_CARD_PAD, UI_ICON_TILE, UI_ICON_TILE_RADIUS,
                        icon, grad_a, grad_b);

    /* 左下：标题（20px 中文标题体）+ 副标题（16px 正文），贴底 16px */
    lv_obj_t *sub = ui_label_create(card, desc, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(sub, UI_CARD_W - UI_CARD_PAD * 2);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, UI_CARD_PAD, -UI_CARD_PAD);

    lv_obj_t *t = ui_label_create(card, title, UI_FONT_CJK_M, UI_COL_TEXT);
    lv_obj_set_width(t, UI_CARD_W - UI_CARD_PAD * 2);
    lv_obj_align_to(t, sub, LV_ALIGN_OUT_TOP_LEFT, 0, -2);

    if (out_desc) *out_desc = sub;
    if (on_click) {
        lv_obj_add_event_cb(card, on_click, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(sub, on_click, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(t, on_click, LV_EVENT_CLICKED, NULL);
    }
    return card;
}

lv_obj_t* ui_home_status_bar_create(lv_obj_t *parent,
                                    lv_obj_t **out_clock,
                                    lv_obj_t **out_vol,
                                    lv_obj_t **out_bat)
{
    lv_obj_t *bar = transparent_box(parent);
    lv_obj_set_size(bar, UI_SCR_W - UI_PAD_X * 2, UI_STATUS_H);
    lv_obj_set_pos(bar, UI_PAD_X, UI_PAD_Y);

    /* 左：时间（设计 13px 粗体白，中文档位取 16px 最接近） */
    lv_obj_t *clock = ui_label_create(bar, "--:--", UI_FONT_SM, UI_COL_TEXT);
    lv_obj_align(clock, LV_ALIGN_LEFT_MID, 0, 0);

    /* 右：从右向左链式对齐（不用 flex + LV_SIZE_CONTENT：4 个 auto 宽度标签放在
     * 自适应容器里 LVGL 8 会把容器算窄，导致 CS43131 / VOL 被裁掉只剩 "%"）。 */
    lv_obj_t *bat = ui_label_create(bar, "95%", UI_FONT_SM, UI_COL_TEXT2);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *charge = ui_label_create(bar, LV_SYMBOL_CHARGE, UI_FONT_SM, UI_COL_ACCENT);
    lv_obj_align_to(charge, bat, LV_ALIGN_OUT_LEFT_MID, -3, 0);

    lv_obj_t *vol = ui_label_create(bar, "VOL 80%", UI_FONT_SM, UI_COL_TEXT2);
    lv_obj_align_to(vol, charge, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    lv_obj_t *hw = ui_label_create(bar, "CS43131", UI_FONT_SM, UI_COL_ACCENT);
    lv_obj_align_to(hw, vol, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    if (out_clock) *out_clock = clock;
    if (out_vol) *out_vol = vol;
    if (out_bat) *out_bat = bat;
    return bar;
}

lv_obj_t* ui_nav_header_create(lv_obj_t *parent, int y,
                               const char *back_text, const char *title,
                               lv_event_cb_t back_cb, lv_obj_t **out_title)
{
    lv_obj_t *row = transparent_box(parent);
    lv_obj_set_size(row, UI_SCR_W - UI_PAD_X * 2, 36);
    /* parent 通常是 ui_page_create() 出来的带 18/14 内边距的页面，子对象坐标从内容区
     * 起算；这里按「屏幕坐标」入参，减去上内边距换算，避免双重内边距把导航头顶到 y=28。 */
    lv_obj_set_pos(row, 0, (y > UI_PAD_Y) ? (y - UI_PAD_Y) : 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    /* 返回药丸：home 图标 + 文案 */
    lv_obj_t *pill = ui_pill_create(row, 0, 0, LV_SIZE_CONTENT, 32,
                                    UI_COL_PILL, UI_COL_PILL);
    lv_obj_set_style_pad_left(pill, 12, 0);
    lv_obj_set_style_pad_right(pill, 12, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 4, 0);

    ui_icon_create(pill, 0, 0, &ui_icon_home, UI_COL_ACCENT);
    ui_label_create(pill, back_text, UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    if (back_cb) lv_obj_add_event_cb(pill, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *t = ui_label_create(row, title, UI_FONT_CJK_M, UI_COL_TEXT);
    if (out_title) *out_title = t;
    return row;
}

/* ---- 档位按钮 ---- */
static void option_restyle(lv_obj_t *pill, bool active)
{
    lv_obj_t *lbl = lv_obj_get_child(pill, 0);
    /* strip_obj() 已经 remove_style_all，bg_opa 默认是 TRANSP；这里必须显式
     * 打开不透明度，否则高亮态只见黑字不见强调色底（看起来像按钮消失了）。 */
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    if (active) {
        lv_obj_set_style_bg_color(pill, UI_COL_ACCENT, 0);
        lv_obj_set_style_border_width(pill, 0, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, UI_COL_BG, 0);
    } else {
        lv_obj_set_style_bg_color(pill, UI_COL_OPTION, 0);
        lv_obj_set_style_border_width(pill, 1, 0);
        lv_obj_set_style_border_color(pill, UI_COL_SURFACE_LINE, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, UI_COL_TEXT2, 0);
    }
}

lv_obj_t* ui_option_pill_create(lv_obj_t *parent, int x, int y, int w, int h,
                                const char *txt, bool active,
                                lv_event_cb_t on_click)
{
    lv_obj_t *pill = lv_obj_create(parent);
    strip_obj(pill);
    lv_obj_set_size(pill, w, h);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_style_radius(pill, 10, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    ui_label_create(pill, txt, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_t *lbl = lv_obj_get_child(pill, 0);
    lv_obj_center(lbl);
    option_restyle(pill, active);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    if (on_click) lv_obj_add_event_cb(pill, on_click, LV_EVENT_CLICKED, NULL);
    return pill;
}

void ui_option_pill_set_active(lv_obj_t *pill, bool active)
{
    if (!pill) return;
    option_restyle(pill, active);
}

/* ---- 圆形按钮 ---- */
lv_obj_t* ui_round_button_create(lv_obj_t *parent, int x, int y, int d,
                                 const lv_img_dsc_t *icon, lv_color_t icon_color,
                                 lv_color_t bg, lv_event_cb_t cb,
                                 lv_obj_t **out_icon)
{
    lv_obj_t *btn = lv_obj_create(parent);
    strip_obj(btn);
    lv_obj_set_size(btn, d, d);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, d / 2, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, UI_COL_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(btn, 245, LV_STATE_PRESSED); /* 轻微按下缩放 */

    lv_obj_t *img = ui_icon_create(btn, 0, 0, icon, icon_color);
    lv_obj_center(img);
    if (out_icon) *out_icon = img;
    if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ---- 徽标 ---- */
lv_obj_t* ui_badge_create(lv_obj_t *parent, int x, int y, const char *txt,
                          lv_color_t fg, lv_color_t bg, lv_color_t border,
                          int h, lv_obj_t **out_label)
{
    lv_obj_t *badge = lv_obj_create(parent);
    strip_obj(badge);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, h);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_style_bg_color(badge, bg, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge, h / 2, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, border, 0);
    lv_obj_set_style_pad_left(badge, 10, 0);
    lv_obj_set_style_pad_right(badge, 10, 0);

    lv_obj_t *lbl = ui_label_create(badge, txt, UI_FONT_TINY, fg);
    lv_obj_center(lbl);
    if (out_label) *out_label = lbl;
    return badge;
}

/* ---- 进度条 ---- */
lv_obj_t* ui_progress_create(lv_obj_t *parent, int x, int y, int w,
                             lv_obj_t **out_fill, lv_obj_t **out_knob)
{
    lv_obj_t *bg = lv_obj_create(parent);
    strip_obj(bg);
    lv_obj_set_size(bg, w, 6);
    lv_obj_set_pos(bg, x, y);
    lv_obj_set_style_bg_color(bg, UI_COL_TRACK, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 3, 0);
    lv_obj_set_style_border_width(bg, 0, 0);

    lv_obj_t *fill = lv_obj_create(bg);
    strip_obj(fill);
    lv_obj_set_size(fill, 0, 6);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_bg_color(fill, UI_COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_add_flag(fill, LV_OBJ_FLAG_IGNORE_LAYOUT);

    lv_obj_t *knob = lv_obj_create(bg);
    strip_obj(knob);
    lv_obj_set_size(knob, 16, 16);
    lv_obj_set_pos(knob, 0, -5);
    lv_obj_set_style_bg_color(knob, UI_COL_TEXT, 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(knob, 8, 0);
    lv_obj_set_style_border_width(knob, 0, 0);
    lv_obj_add_flag(knob, LV_OBJ_FLAG_IGNORE_LAYOUT);

    if (out_fill) *out_fill = fill;
    if (out_knob) *out_knob = knob;
    return bg;
}

void ui_progress_set_percent(lv_obj_t *fill, lv_obj_t *knob, int percent)
{
    if (!fill) return;
    lv_obj_t *bg = lv_obj_get_parent(fill);
    if (!bg) return;
    int w = lv_obj_get_width(bg);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int fw = w * percent / 100;
    lv_obj_set_width(fill, fw);
    if (knob) lv_obj_set_x(knob, fw - 8);
}
