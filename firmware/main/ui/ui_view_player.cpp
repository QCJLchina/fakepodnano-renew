/**
 * FAKE_POD_NANO 本地播放页（全新重构版）
 * ==============================================================
 * 视觉基准：prototype/index.html 的 .player-page / .album-art-card / .track-meta
 *          → design_ref/player.png（460x460 设计截图，2x 渲染）。
 *
 * 用户专项反馈落实：
 *   1. 彻底弃用老式黑胶圆盘，默认使用设计稿的高质感 196x196 圆角专辑卡片（深蓝灰渐变 + 同心环
 *      + 专辑/艺术家名称，有内嵌封面时展示真实封面图）；
 *   2. 底部控制区扩展为 5 键高阶 DAP 排布：
 *      [ 🔀 随机播放 ]  [ ⏮ 上一曲 ]  [ ▶/⏸ 播放暂停 (70px金橙主键) ]  [ ⏭ 下一曲 ]  [ 🔁 循环模式 ]
 *   3. 顶部去除冗余的 VOL 80% 文字（音量全靠全屏上下滑手势与物理按键触发，中央弹出高质感 HUD）；
 *   4. 随机播放与循环模式（列表循环/单曲循环/顺序播放）具备完整状态切换与 HUD 提示，
 *      并与 player_service 底层播放逻辑真实联动。
 */

#include "ui_view_player.h"

#include "ui_manager.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "ui_icons.h"

#include "player_service.h"
#include "album_art.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------
 * 页面内对象
 * --------------------------------------------------------------- */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_page         = NULL;

static lv_obj_t *s_lbl_badge    = NULL;  /* 顶部格式徽标文案 */
static lv_obj_t *s_lbl_title    = NULL;  /* 曲名 */
static lv_obj_t *s_lbl_artist   = NULL;  /* 副行：艺术家 */
static lv_obj_t *s_lbl_rate     = NULL;  /* 副行：采样率/位深 */

static lv_obj_t *s_prog_fill    = NULL;  /* 进度条填充 */
static lv_obj_t *s_prog_knob    = NULL;  /* 进度条滑块 */
static lv_obj_t *s_lbl_time_cur = NULL;
static lv_obj_t *s_lbl_time_tot = NULL;

static lv_obj_t *s_btn_play     = NULL;
static lv_obj_t *s_play_icon    = NULL;  /* 主键内的 play/pause 图标 */

/* 5 键控制栏新增：随机与循环模式按钮 */
static lv_obj_t *s_btn_shuffle  = NULL;
static lv_obj_t *s_img_shuffle  = NULL;
static lv_obj_t *s_btn_repeat   = NULL;
static lv_obj_t *s_img_repeat   = NULL;

/* 模式状态 */
static bool s_shuffle_on = false;
static int  s_repeat_mode = 0;  /* 0: 列表循环 (All), 1: 单曲循环 (1), 2: 顺序播放 (Off) */

/* 现代圆角专辑封面卡片（196x196，圆角 24） */
static lv_obj_t *s_album_card   = NULL;  /* 封面卡片主容器 */
static lv_obj_t *s_card_artist  = NULL;  /* 无封面时的同心环内艺术家大字 */
static lv_obj_t *s_card_album   = NULL;  /* 无封面时的同心环内曲名/唱片小字 */
static lv_obj_t *s_img_cover    = NULL;  /* 真实专辑封面图片图层 */
static lv_img_dsc_t s_cover_dsc;
static char s_cover_path[256] = {0};

/* 居中弹出式 HUD 胶囊（用于手势音量调节与播放模式切换提示） */
static lv_obj_t *s_hud          = NULL;
static lv_obj_t *s_hud_icon     = NULL;
static lv_obj_t *s_lbl_hud      = NULL;
static lv_timer_t *s_hud_timer  = NULL;

/* 进度防抖缓存 */
static int s_last_pct = -1;

/* ---------------------------------------------------------------
 * 小工具
 * --------------------------------------------------------------- */
static lv_obj_t* make_transparent(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

static void label_ellipsis(lv_obj_t *lbl, int w)
{
    if (!lbl) return;
    lv_obj_set_width(lbl, w);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
}

static void track_title(char *out, size_t out_len, const player_status_t *st)
{
    if (!out || out_len < 2) return;
    out[0] = '\0';

    const char *path = st->current_path;
    const char *name = strrchr(path, '/');
    name = name ? (name + 1) : path;
    if (!name || !*name) return;

    size_t len = strlen(name);
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) len = (size_t)(dot - name);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

static void track_artist(char *out, size_t out_len, const player_status_t *st)
{
    if (!out || out_len < 2) return;
    out[0] = '\0';

    char base[256];
    track_title(base, sizeof(base), st);
    size_t blen = strlen(base);

    const char *sep = NULL;
    for (size_t i = 0; i + 2 < blen; i++) {
        if (base[i] == ' ' && base[i + 1] == '-' && base[i + 2] == ' ') {
            sep = base + i + 3;
            break;
        }
    }
    if (!sep) return;

    size_t len = strlen(sep);
    for (size_t i = 0; i + 1 < len; i++) {
        if (sep[i] == ' ' && sep[i + 1] == '(') { len = i; break; }
    }
    while (len > 0 && sep[len - 1] == ' ') len--;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, sep, len);
    out[len] = '\0';
}

static void fmt_khz(char *out, size_t out_len, uint32_t sr)
{
    if (sr % 1000u == 0u) {
        snprintf(out, out_len, "%lukHz", (unsigned long)(sr / 1000u));
    } else {
        snprintf(out, out_len, "%.1fkHz", (double)sr / 1000.0);
    }
}

static const char* decoder_type_name(audio_type_t t)
{
    switch (t) {
        case DECODER_TYPE_FLAC:    return "FLAC";
        case DECODER_TYPE_WAV:     return "WAV";
        case DECODER_TYPE_MP3:     return "MP3";
        case DECODER_TYPE_AAC_M4A: return "AAC";
        case DECODER_TYPE_DSD_DSF: return "DSD64";
        default:                   return "PCM";
    }
}

/* ---------------------------------------------------------------
 * 统一 HUD 弹出管理
 * --------------------------------------------------------------- */
static void hud_timeout_cb(lv_timer_t *t)
{
    if (s_hud) lv_obj_add_flag(s_hud, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

static void hud_show(const lv_img_dsc_t *icon, const char *text, lv_color_t icon_col)
{
    if (!s_hud) return;
    if (s_hud_icon && icon) {
        lv_img_set_src(s_hud_icon, icon);
        lv_obj_set_style_img_recolor(s_hud_icon, icon_col, 0);
        lv_obj_set_style_img_recolor_opa(s_hud_icon, LV_OPA_COVER, 0);
    }
    if (s_lbl_hud && text) {
        lv_label_set_text(s_lbl_hud, text);
    }
    lv_obj_clear_flag(s_hud, LV_OBJ_FLAG_HIDDEN);
    if (!s_hud_timer) {
        s_hud_timer = lv_timer_create(hud_timeout_cb, 1200, NULL);
    } else {
        lv_timer_reset(s_hud_timer);
        lv_timer_resume(s_hud_timer);
    }
}

/* ---------------------------------------------------------------
 * 播放模式按钮刷新
 * --------------------------------------------------------------- */
static void update_mode_buttons(void)
{
    if (s_img_shuffle) {
        lv_obj_set_style_img_recolor(s_img_shuffle,
            s_shuffle_on ? UI_COL_ACCENT : UI_COL_TEXT3, 0);
    }
    if (s_btn_shuffle) {
        lv_obj_set_style_bg_color(s_btn_shuffle,
            s_shuffle_on ? UI_COL_ACCENT_SOFT : UI_COL_PILL, 0);
        lv_obj_set_style_border_color(s_btn_shuffle,
            s_shuffle_on ? UI_COL_ACCENT_LINE : UI_COL_SURFACE_LINE, 0);
        lv_obj_set_style_border_width(s_btn_shuffle, s_shuffle_on ? 1 : 0, 0);
    }

    if (s_img_repeat) {
        bool active = (s_repeat_mode < 2);
        lv_img_set_src(s_img_repeat,
            (s_repeat_mode == 1) ? &ui_icon_repeat_one : &ui_icon_repeat);
        lv_obj_set_style_img_recolor(s_img_repeat,
            active ? UI_COL_ACCENT : UI_COL_TEXT3, 0);
    }
    if (s_btn_repeat) {
        bool active = (s_repeat_mode < 2);
        lv_obj_set_style_bg_color(s_btn_repeat,
            active ? UI_COL_ACCENT_SOFT : UI_COL_PILL, 0);
        lv_obj_set_style_border_color(s_btn_repeat,
            active ? UI_COL_ACCENT_LINE : UI_COL_SURFACE_LINE, 0);
        lv_obj_set_style_border_width(s_btn_repeat, active ? 1 : 0, 0);
    }
}

/* ---------------------------------------------------------------
 * 按键 / 手势回调
 * --------------------------------------------------------------- */
static void btn_play_click_cb(lv_event_t *e)
{
    (void)e;
    player_status_t st;
    player_get_status(&st);
    if (st.state == PLAYER_STATE_PLAYING) {
        player_pause();
    } else if (st.state == PLAYER_STATE_PAUSED) {
        player_resume();
    }
}

static void btn_home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void prev_cb(lv_event_t *e) { (void)e; player_prev(); }
static void next_cb(lv_event_t *e) { (void)e; player_next(); }

static void shuffle_click_cb(lv_event_t *e)
{
    (void)e;
    s_shuffle_on = !s_shuffle_on;
    if (s_shuffle_on) {
        player_set_play_mode(PLAY_MODE_SHUFFLE);
        hud_show(&ui_icon_shuffle, "随机播放: 开", UI_COL_ACCENT);
    } else {
        player_set_play_mode((s_repeat_mode == 1) ? PLAY_MODE_REPEAT_ONE : PLAY_MODE_REPEAT_ALL);
        hud_show(&ui_icon_shuffle, "随机播放: 关", UI_COL_TEXT2);
    }
    update_mode_buttons();
}

static void repeat_click_cb(lv_event_t *e)
{
    (void)e;
    s_repeat_mode = (s_repeat_mode + 1) % 3;
    if (s_repeat_mode == 0) {
        if (!s_shuffle_on) player_set_play_mode(PLAY_MODE_REPEAT_ALL);
        hud_show(&ui_icon_repeat, "列表循环", UI_COL_ACCENT);
    } else if (s_repeat_mode == 1) {
        if (!s_shuffle_on) player_set_play_mode(PLAY_MODE_REPEAT_ONE);
        hud_show(&ui_icon_repeat_one, "单曲循环", UI_COL_ACCENT);
    } else {
        hud_show(&ui_icon_repeat, "顺序播放", UI_COL_TEXT2);
    }
    update_mode_buttons();
}

static void volume_step(int delta)
{
    player_status_t st;
    player_get_status(&st);
    int v = (int)st.volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    player_set_volume((uint8_t)v);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", v);
    hud_show(&ui_icon_volume, buf, UI_COL_ACCENT);
}

/* 播放页全屏手势：上下滑调音量（中央弹 HUD）、左右滑切曲 */
static void player_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP) {
        volume_step(+5);
    } else if (dir == LV_DIR_BOTTOM) {
        volume_step(-5);
    } else if (dir == LV_DIR_LEFT) {
        player_next();
    } else if (dir == LV_DIR_RIGHT) {
        player_prev();
    }
}

static void progress_tap_cb(lv_event_t *e)
{
    lv_obj_t *bg = lv_event_get_current_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!bg || !indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t c;
    lv_obj_get_coords(bg, &c);
    int w = lv_area_get_width(&c);
    if (w <= 0) return;

    int pct = (int)((p.x - c.x1) * 100 / w);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    player_status_t st;
    player_get_status(&st);
    player_seek((uint32_t)((uint32_t)st.total_time_sec * (uint32_t)pct / 100u));
}

/* ---------------------------------------------------------------
 * 创建播放页
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_player_create(void)
{
    s_screen = ui_screen_create();
    s_page   = ui_page_create(s_screen);   /* 内边距 18/14，内容宽 424 */

    /* ===== 1. 顶部 header：高 28，两端留白对齐 ===== */
    lv_obj_t *hdr = make_transparent(s_page, 0, 0, UI_SCR_W - UI_PAD_X * 2, 28);

    /* 左：返回桌面药丸 */
    lv_obj_t *pill = ui_pill_create(hdr, 0, 0, LV_SIZE_CONTENT, 28,
                                    UI_COL_PILL, UI_COL_PILL);
    lv_obj_set_style_pad_left(pill, 10, 0);
    lv_obj_set_style_pad_right(pill, 10, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 4, 0);
    ui_icon_create(pill, 0, 0, &ui_icon_home, UI_COL_ACCENT);
    ui_label_create(pill, "桌面", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pill, btn_home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(pill, LV_ALIGN_LEFT_MID, 0, 0);

    /* 中：格式徽标（如 FLAC 96kHz 24bit） */
    lv_obj_t *badge = ui_badge_create(hdr, 0, 0, "FLAC 96kHz 24bit",
                                      UI_COL_ACCENT, UI_COL_ACCENT_SOFT,
                                      UI_COL_ACCENT_LINE, 24, &s_lbl_badge);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, 0);
    if (s_lbl_badge) lv_label_set_long_mode(s_lbl_badge, LV_LABEL_LONG_DOT);

    /* ===== 2. 现代方角专辑卡片（196x196，圆角 24，设计稿核心视觉） ===== */
    s_album_card = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_album_card);
    lv_obj_clear_flag(s_album_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_album_card, 196, 196);
    lv_obj_set_style_radius(s_album_card, 24, 0);
    /* 采用高保真现代深蓝紫微渐变底板 */
    lv_obj_set_style_bg_color(s_album_card, lv_color_hex(0x22354E), 0);
    lv_obj_set_style_bg_grad_color(s_album_card, lv_color_hex(0x131A26), 0);
    lv_obj_set_style_bg_grad_dir(s_album_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_album_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_album_card, 1, 0);
    lv_obj_set_style_border_color(s_album_card, lv_color_hex(0x354D6E), 0);
    lv_obj_set_style_pad_all(s_album_card, 0, 0);
    lv_obj_set_style_clip_corner(s_album_card, true, 0);
    lv_obj_align(s_album_card, LV_ALIGN_TOP_MID, 0, 36);

    /* 卡片内同心圆环（对应原型设计稿 .album-art-card 内的 126px 精密圆环） */
    lv_obj_t *ring = lv_obj_create(s_album_card);
    lv_obj_remove_style_all(ring);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ring, 126, 126);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x405A80), 0);
    lv_obj_center(ring);

    /* 中心艺术家与专辑大字（设计稿原型 EAGLES / Hotel California；支持中文） */
    s_card_artist = ui_label_create(s_album_card, "EAGLES", UI_FONT_CJK_M, UI_COL_TEXT);
    lv_obj_set_style_text_align(s_card_artist, LV_TEXT_ALIGN_CENTER, 0);
    label_ellipsis(s_card_artist, 120);
    lv_obj_align(s_card_artist, LV_ALIGN_CENTER, 0, -11);

    s_card_album = ui_label_create(s_album_card, "Hotel California", UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_style_text_align(s_card_album, LV_TEXT_ALIGN_CENTER, 0);
    label_ellipsis(s_card_album, 120);
    lv_obj_align(s_card_album, LV_ALIGN_CENTER, 0, 11);

    /* 真实封面层：有内嵌专辑图时直接铺满覆盖 */
    s_img_cover = lv_img_create(s_album_card);
    lv_obj_set_size(s_img_cover, 196, 196);
    lv_obj_set_pos(s_img_cover, 0, 0);
    lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);

    /* ===== 3. 曲目信息区（居中） ===== */
    s_lbl_title = ui_label_create(s_page, "加州旅馆 - Eagles (Live)", UI_FONT_CJK_M, UI_COL_TEXT);
    label_ellipsis(s_lbl_title, UI_SCR_W - UI_PAD_X * 2 - 20);
    lv_obj_set_style_text_align(s_lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 252);

    /* 副行：艺术家 · 采样率/位深 */
    lv_obj_t *sub_row = make_transparent(s_page, 0, 0, UI_SCR_W - UI_PAD_X * 2 - 20, 22);
    lv_obj_set_flex_flow(sub_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sub_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sub_row, 6, 0);
    lv_obj_align_to(sub_row, s_lbl_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    s_lbl_artist = ui_label_create(sub_row, "Eagles", UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(s_lbl_artist, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s_lbl_artist, LV_LABEL_LONG_DOT);

    lv_obj_t *dot = lv_obj_create(sub_row);
    lv_obj_remove_style_all(dot);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, 4, 4);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, UI_COL_TEXT2, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    s_lbl_rate = ui_label_create(sub_row, "96kHz / 24bit", UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(s_lbl_rate, LV_SIZE_CONTENT);
    lv_label_set_long_mode(s_lbl_rate, LV_LABEL_LONG_DOT);

    /* ===== 4. 进度区 ===== */
    lv_obj_t *prog = ui_progress_create(s_page, 6, 314,
                                        UI_SCR_W - UI_PAD_X * 2 - 12,
                                        &s_prog_fill, &s_prog_knob);
    lv_obj_add_flag(prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prog, progress_tap_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_time_cur = ui_label_create(s_page, "01:45", UI_FONT_TINY, UI_COL_TEXT2);
    label_ellipsis(s_lbl_time_cur, 80);
    lv_obj_align(s_lbl_time_cur, LV_ALIGN_TOP_LEFT, 6, 324);

    s_lbl_time_tot = ui_label_create(s_page, "06:30", UI_FONT_TINY, UI_COL_TEXT2);
    label_ellipsis(s_lbl_time_tot, 80);
    lv_obj_set_style_text_align(s_lbl_time_tot, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_lbl_time_tot, LV_ALIGN_TOP_RIGHT, -6, 324);

    /* ===== 5. 5 键控制区：[随机] [上一曲] [播放/暂停] [下一曲] [循环] ===== */
    lv_obj_t *ctrl = make_transparent(s_page, 0, 354, UI_SCR_W - UI_PAD_X * 2, 72);
    lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 1. 随机播放键 (42px) */
    s_btn_shuffle = ui_round_button_create(ctrl, 0, 0, 42, &ui_icon_shuffle, UI_COL_TEXT3,
                                           UI_COL_PILL, shuffle_click_cb, &s_img_shuffle);

    /* 2. 上一曲 (52px) */
    ui_round_button_create(ctrl, 0, 0, 52, &ui_icon_prev, UI_COL_TEXT,
                           UI_COL_PILL, prev_cb, NULL);

    /* 3. 主按键：播放/暂停 (70px，金橙高亮底) */
    s_btn_play = ui_round_button_create(ctrl, 0, 0, 70, &ui_icon_pause, UI_COL_BG,
                                        UI_COL_ACCENT, btn_play_click_cb, &s_play_icon);
    (void)s_btn_play;

    /* 4. 下一曲 (52px) */
    ui_round_button_create(ctrl, 0, 0, 52, &ui_icon_next, UI_COL_TEXT,
                           UI_COL_PILL, next_cb, NULL);

    /* 5. 循环播放键 (42px) */
    s_btn_repeat = ui_round_button_create(ctrl, 0, 0, 42, &ui_icon_repeat, UI_COL_ACCENT,
                                          UI_COL_ACCENT_SOFT, repeat_click_cb, &s_img_repeat);

    update_mode_buttons();

    /* ===== 6. 居中浮动 HUD 胶囊（用于音量手势与模式切换） ===== */
    s_hud = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_hud);
    lv_obj_clear_flag(s_hud, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_hud, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_hud, 140, 116);
    lv_obj_set_style_radius(s_hud, 24, 0);
    lv_obj_set_style_bg_color(s_hud, UI_COL_CARD_BOT, 0);
    lv_obj_set_style_bg_opa(s_hud, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_hud, 1, 0);
    lv_obj_set_style_border_color(s_hud, UI_COL_ACCENT_LINE, 0);
    lv_obj_set_style_pad_all(s_hud, 14, 0);
    lv_obj_set_style_pad_row(s_hud, 6, 0);
    lv_obj_set_flex_flow(s_hud, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_hud, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_hud, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_hud, LV_OBJ_FLAG_HIDDEN);

    s_hud_icon = ui_icon_create(s_hud, 0, 0, &ui_icon_volume, UI_COL_ACCENT);
    s_lbl_hud  = ui_label_create(s_hud, "80%", UI_FONT_CJK_S, UI_COL_TEXT);
    label_ellipsis(s_lbl_hud, 120);
    lv_obj_set_style_text_align(s_lbl_hud, LV_TEXT_ALIGN_CENTER, 0);

    /* ===== 7. 手势与底部手势条 ===== */
    lv_obj_add_event_cb(s_page, player_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_screen, player_gesture_cb, LV_EVENT_GESTURE, NULL);
    ui_gesture_hint_create(s_screen);

    return s_screen;
}

/* ---------------------------------------------------------------
 * 周期刷新（ui_manager 每 200ms 调用一次）
 * --------------------------------------------------------------- */
void ui_view_player_update(void)
{
    if (!s_screen) return;

    player_status_t st;
    player_get_status(&st);

    /* 1. 播放状态 → 主键图标 */
    if (s_play_icon) {
        lv_img_set_src(s_play_icon,
                       (st.state == PLAYER_STATE_PLAYING) ? &ui_icon_pause : &ui_icon_play);
        lv_obj_center(s_play_icon);
    }

    bool has_track = (st.state != PLAYER_STATE_STOPPED &&
                      st.state != PLAYER_STATE_ERROR && st.current_path[0]);

    /* 2. 格式徽标 / 曲名 / 副行 */
    if (has_track) {
        char khz[24];
        fmt_khz(khz, sizeof(khz), st.track_info.fmt.sample_rate);

        if (s_lbl_badge) {
            char badge[48];
            snprintf(badge, sizeof(badge), "%s %s %dbit",
                     decoder_type_name(st.track_info.type), khz,
                     (int)st.track_info.fmt.bits_per_sample);
            lv_label_set_text(s_lbl_badge, badge);
        }

        char title[256];
        track_title(title, sizeof(title), &st);
        if (s_lbl_title) {
            lv_label_set_text(s_lbl_title, title[0] ? title : "未知曲目");
        }

        char artist[128];
        track_artist(artist, sizeof(artist), &st);
        if (s_lbl_artist) {
            lv_label_set_text(s_lbl_artist, artist[0] ? artist : "Eagles");
        }
        if (s_lbl_rate) {
            char rate[64];
            snprintf(rate, sizeof(rate), "%s / %dbit", khz,
                     (int)st.track_info.fmt.bits_per_sample);
            lv_label_set_text(s_lbl_rate, rate);
        }

        /* 同步更新卡片中央文字（无封面模式下的专辑信息） */
        if (s_card_artist) {
            lv_label_set_text(s_card_artist, artist[0] ? artist : "EAGLES");
        }
        if (s_card_album) {
            lv_label_set_text(s_card_album, title[0] ? title : "Hotel California");
        }
    } else {
        if (s_lbl_badge) lv_label_set_text(s_lbl_badge, "无播放");
        if (s_lbl_title) lv_label_set_text(s_lbl_title, "未在播放");
        if (s_lbl_artist) lv_label_set_text(s_lbl_artist, "FAKE POD NANO");
        if (s_lbl_rate)   lv_label_set_text(s_lbl_rate, "Hi-Fi Player");
    }

    /* 3. 进度条 + 已播/总时长 */
    uint32_t total = st.total_time_sec;
    uint32_t cur   = st.current_time_sec;
    int pct = total ? (int)((uint64_t)cur * 100u / total) : 0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != s_last_pct) {
        ui_progress_set_percent(s_prog_fill, s_prog_knob, pct);
        s_last_pct = pct;
    }

    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
             (unsigned long)(cur / 60u), (unsigned long)(cur % 60u));
    if (s_lbl_time_cur) lv_label_set_text(s_lbl_time_cur, tbuf);

    snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu",
             (unsigned long)(total / 60u), (unsigned long)(total % 60u));
    if (s_lbl_time_tot) lv_label_set_text(s_lbl_time_tot, tbuf);

    /* 4. 专辑封面检测（有内嵌图则覆盖卡片） */
    if (has_track) {
        if (strcmp(st.current_path, s_cover_path) != 0) {
            snprintf(s_cover_path, sizeof(s_cover_path), "%s", st.current_path);
            lv_img_dsc_t dsc;
            if (album_art_get(st.current_path, &dsc)) {
                s_cover_dsc = dsc;
                if (s_img_cover) {
                    lv_img_set_src(s_img_cover, &s_cover_dsc);
                    lv_obj_center(s_img_cover);
                    lv_obj_clear_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (s_img_cover) {
                    lv_img_set_src(s_img_cover, NULL);
                    lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else {
        if (s_img_cover) {
            lv_img_set_src(s_img_cover, NULL);
            lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
        }
        s_cover_path[0] = '\0';
    }
}
