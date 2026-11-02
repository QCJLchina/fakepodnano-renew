/**
 * FAKE_POD_NANO 全新现代化主播放器界面实现
 */

#include "ui_view_player.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "player_service.h"
#include "album_art.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_badge = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_artist = NULL;
static lv_obj_t *s_slider_progress = NULL;
static lv_obj_t *s_lbl_time_cur = NULL;
static lv_obj_t *s_lbl_time_tot = NULL;
static lv_obj_t *s_btn_play = NULL;
static lv_obj_t *s_lbl_play_icon = NULL;
static lv_obj_t *s_btn_prev = NULL;
static lv_obj_t *s_btn_next = NULL;
static lv_obj_t *s_btn_vol_dn = NULL;
static lv_obj_t *s_btn_vol_up = NULL;
static lv_obj_t *s_disc = NULL;        /* 黑胶唱盘（无封面时显示） */
static lv_obj_t *s_img_cover = NULL;   /* 专辑封面图层 */
static lv_img_dsc_t s_cover_dsc;
static char s_cover_path[256] = {0};
static bool s_slider_dragging = false;

static void btn_play_click_cb(lv_event_t *e)
{
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

/* ---------------------------------------------------------------
 * 音量 / 切曲 控制（按钮 + 手势）
 * --------------------------------------------------------------- */
static lv_obj_t *s_lbl_vol = NULL;
static lv_timer_t *s_vol_hud_timer = NULL;

static void vol_hud_timeout_cb(lv_timer_t *t)
{
    if (s_lbl_vol) lv_obj_add_flag(s_lbl_vol, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(t);
}

static void vol_hud_show(int v)
{
    if (!s_lbl_vol) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "音量 %d%%", v);
    lv_label_set_text(s_lbl_vol, buf);
    lv_obj_clear_flag(s_lbl_vol, LV_OBJ_FLAG_HIDDEN);
    if (!s_vol_hud_timer) {
        s_vol_hud_timer = lv_timer_create(vol_hud_timeout_cb, 1500, NULL);
    } else {
        lv_timer_reset(s_vol_hud_timer);
        lv_timer_resume(s_vol_hud_timer);
    }
}

static void volume_step(int delta)
{
    player_status_t st;
    player_get_status(&st);
    int v = (int)st.volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    player_set_volume((uint8_t)v);
    vol_hud_show(v);
}

static void vol_dn_cb(lv_event_t *e) { (void)e; volume_step(-5); }
static void vol_up_cb(lv_event_t *e) { (void)e; volume_step(+5); }
static void prev_cb(lv_event_t *e)   { (void)e; player_prev(); }
static void next_cb(lv_event_t *e)   { (void)e; player_next(); }

/* 播放页手势：上下滑调音量、左右滑切歌 */
static void player_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
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

static void slider_progress_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_slider_dragging = true;
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_slider_dragging = false;
        lv_obj_t *slider = lv_event_get_target(e);
        player_seek((uint32_t)lv_slider_get_value(slider));
    }
}

lv_obj_t* ui_view_player_create(void)
{
    /* 1. 根页面：AMOLED 深度纯黑背景，省电且对比度极致 */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);

    /* 2. 顶部状态栏：格式徽标胶囊 (Format Badge) */
    s_lbl_badge = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_lbl_badge, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_text_font(s_lbl_badge, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_lbl_badge, "CS43131 Hi-Fi Ready");
    lv_obj_align(s_lbl_badge, LV_ALIGN_TOP_MID, 0, 16);

    /* 3. 屏幕中心：艺术感黑胶圆形质感唱盘 (Vinyl Disc Card) */
    lv_obj_t *disc = lv_obj_create(s_screen);
    s_disc = disc;
    lv_obj_set_size(disc, 200, 200);
    lv_obj_align(disc, LV_ALIGN_CENTER, 0, -35);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_make(18, 18, 22), 0);
    lv_obj_set_style_border_color(disc, lv_color_make(45, 45, 55), 0);
    lv_obj_set_style_border_width(disc, 4, 0);
    lv_obj_set_style_shadow_color(disc, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_shadow_width(disc, 25, 0);
    lv_obj_set_style_shadow_spread(disc, 2, 0);
    lv_obj_set_style_shadow_opa(disc, LV_OPA_30, 0);

    /* 唱盘中心金色轴心小圈 */
    lv_obj_t *inner_disc = lv_obj_create(disc);
    lv_obj_set_size(inner_disc, 64, 64);
    lv_obj_center(inner_disc);
    lv_obj_set_style_radius(inner_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner_disc, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_border_width(inner_disc, 0, 0);

    /* 4. 歌曲名称与艺术家/码率展示 */
    s_lbl_title = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), 0);
    /* 曲名可能含中文/日文/韩文文件名，用 16px 全量 CJK 字库保证都能显示 */
    lv_obj_set_style_text_font(s_lbl_title, &ui_font_cjk_16, 0);
    lv_label_set_text(s_lbl_title, "Nano Lossless");
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 95);

    s_lbl_artist = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_lbl_artist, lv_color_make(160, 160, 175), 0);
    lv_obj_set_style_text_font(s_lbl_artist, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_lbl_artist, "Cirrus Logic CS43131");
    lv_obj_align(s_lbl_artist, LV_ALIGN_CENTER, 0, 122);

    /* 5. 进度条与播放时长 */
    s_slider_progress = lv_slider_create(s_screen);
    lv_obj_set_size(s_slider_progress, 340, 6);
    lv_obj_align(s_slider_progress, LV_ALIGN_BOTTOM_MID, 0, -85);
    lv_obj_set_style_bg_color(s_slider_progress, lv_color_make(40, 40, 48), 0);
    lv_obj_set_style_bg_color(s_slider_progress, lv_palette_main(LV_PALETTE_AMBER), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_progress, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_progress, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_progress, slider_progress_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_slider_progress, slider_progress_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_slider_progress, slider_progress_cb, LV_EVENT_PRESS_LOST, NULL);

    s_lbl_time_cur = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_lbl_time_cur, lv_color_make(140, 140, 150), 0);
    lv_obj_set_style_text_font(s_lbl_time_cur, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_lbl_time_cur, "00:00");
    lv_obj_align_to(s_lbl_time_cur, s_slider_progress, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    s_lbl_time_tot = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_lbl_time_tot, lv_color_make(140, 140, 150), 0);
    lv_obj_set_style_text_font(s_lbl_time_tot, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_lbl_time_tot, "00:00");
    lv_obj_align_to(s_lbl_time_tot, s_slider_progress, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 6);

    /* 6. 底部控制区：上一曲 / 播放暂停 / 下一曲 + 音量 ± */
    s_btn_prev = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_prev, 56, 56);
    lv_obj_align(s_btn_prev, LV_ALIGN_BOTTOM_MID, -84, -20);
    lv_obj_set_style_radius(s_btn_prev, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_make(34, 34, 42), 0);
    lv_obj_set_style_shadow_width(s_btn_prev, 0, 0);
    lv_obj_add_event_cb(s_btn_prev, prev_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_prev = lv_label_create(s_btn_prev);
    lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_prev, lv_color_white(), 0);
    lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
    lv_obj_center(lbl_prev);

    s_btn_play = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_play, 72, 72);
    lv_obj_align(s_btn_play, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(s_btn_play, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_play, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_shadow_width(s_btn_play, 0, 0);
    lv_obj_add_event_cb(s_btn_play, btn_play_click_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_play_icon = lv_label_create(s_btn_play);
    lv_obj_set_style_text_color(s_lbl_play_icon, lv_color_black(), 0);
    lv_obj_set_style_text_font(s_lbl_play_icon, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_lbl_play_icon, LV_SYMBOL_PLAY);
    lv_obj_center(s_lbl_play_icon);

    s_btn_next = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_next, 56, 56);
    lv_obj_align(s_btn_next, LV_ALIGN_BOTTOM_MID, 84, -20);
    lv_obj_set_style_radius(s_btn_next, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_next, lv_color_make(34, 34, 42), 0);
    lv_obj_set_style_shadow_width(s_btn_next, 0, 0);
    lv_obj_add_event_cb(s_btn_next, next_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = lv_label_create(s_btn_next);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_next, lv_color_white(), 0);
    lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
    lv_obj_center(lbl_next);

    /* 音量 ± 小圆键 */
    s_btn_vol_dn = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_vol_dn, 44, 44);
    lv_obj_align(s_btn_vol_dn, LV_ALIGN_BOTTOM_LEFT, 16, -28);
    lv_obj_set_style_radius(s_btn_vol_dn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_vol_dn, lv_color_make(30, 30, 38), 0);
    lv_obj_set_style_shadow_width(s_btn_vol_dn, 0, 0);
    lv_obj_add_event_cb(s_btn_vol_dn, vol_dn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_vd = lv_label_create(s_btn_vol_dn);
    lv_obj_set_style_text_font(lbl_vd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_vd, lv_color_white(), 0);
    lv_label_set_text(lbl_vd, LV_SYMBOL_MINUS);
    lv_obj_center(lbl_vd);

    s_btn_vol_up = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_vol_up, 44, 44);
    lv_obj_align(s_btn_vol_up, LV_ALIGN_BOTTOM_RIGHT, -16, -28);
    lv_obj_set_style_radius(s_btn_vol_up, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_btn_vol_up, lv_color_make(30, 30, 38), 0);
    lv_obj_set_style_shadow_width(s_btn_vol_up, 0, 0);
    lv_obj_add_event_cb(s_btn_vol_up, vol_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_vu = lv_label_create(s_btn_vol_up);
    lv_obj_set_style_text_font(lbl_vu, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_vu, lv_color_white(), 0);
    lv_label_set_text(lbl_vu, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_vu);

    /* 音量 HUD（右上角短暂显示） */
    s_lbl_vol = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_vol, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_lbl_vol, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(s_lbl_vol, "");
    lv_obj_align(s_lbl_vol, LV_ALIGN_TOP_RIGHT, -16, 46);
    lv_obj_add_flag(s_lbl_vol, LV_OBJ_FLAG_HIDDEN);

    /* 手势：上下滑调音量、左右滑切歌 */
    lv_obj_add_event_cb(s_screen, player_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 专辑封面图层（默认隐藏，有内嵌封面时显示，替代黑胶唱盘） */
    s_img_cover = lv_img_create(s_screen);
    lv_obj_align(s_img_cover, LV_ALIGN_CENTER, 0, -35);
    lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);

    /* 7. 左上角「桌面」返回键（UI_DESIGN_SPEC：一键回桌面） */
    lv_obj_t *btn_home = lv_btn_create(s_screen);
    lv_obj_set_size(btn_home, 76, 40);
    lv_obj_align(btn_home, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_radius(btn_home, 10, 0);
    lv_obj_set_style_bg_color(btn_home, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_home, 0, 0);
    lv_obj_add_event_cb(btn_home, btn_home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_obj_set_style_text_font(lbl_home, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_home, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(lbl_home, LV_SYMBOL_HOME);
    lv_obj_center(lbl_home);

    return s_screen;
}

void ui_view_player_update(void)
{
    if (!s_screen) return;

    player_status_t st;
    player_get_status(&st);

    /* 1. 更新播放状态与图标 */
    if (st.state == PLAYER_STATE_PLAYING) {
        lv_label_set_text(s_lbl_play_icon, LV_SYMBOL_PAUSE);
    } else {
        lv_label_set_text(s_lbl_play_icon, LV_SYMBOL_PLAY);
    }

    /* 2. 更新曲目名与格式 */
    if (st.state != PLAYER_STATE_STOPPED && st.current_path[0]) {
        /* 从路径提取文件名 */
        const char *fname = strrchr(st.current_path, '/');
        fname = fname ? (fname + 1) : st.current_path;
        lv_label_set_text(s_lbl_title, fname);

        /* 格式徽标 */
        char badge_buf[64];
        const char *typeName = "PCM";
        switch (st.track_info.type) {
            case DECODER_TYPE_FLAC: typeName = "FLAC"; break;
            case DECODER_TYPE_WAV:  typeName = "WAV"; break;
            case DECODER_TYPE_MP3:  typeName = "MP3"; break;
            case DECODER_TYPE_DSD_DSF: typeName = "DSD64 (DoP)"; break;
            case DECODER_TYPE_AAC_M4A: typeName = "AAC"; break;
            default: break;
        }
        snprintf(badge_buf, sizeof(badge_buf), "%s | %lu Hz | %d bit",
                 typeName, (unsigned long)st.track_info.fmt.sample_rate,
                 st.track_info.fmt.bits_per_sample);
        lv_label_set_text(s_lbl_badge, badge_buf);
    }

    /* 3. 更新进度条与时间。拖动期间不回写，否则手指会被播放进度拽回去。 */
    if (!s_slider_dragging) {
        lv_slider_set_range(s_slider_progress, 0, st.total_time_sec ? st.total_time_sec : 100);
        lv_slider_set_value(s_slider_progress, st.current_time_sec, LV_ANIM_OFF);
    }

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
             (unsigned long)(st.current_time_sec / 60),
             (unsigned long)(st.current_time_sec % 60));
    lv_label_set_text(s_lbl_time_cur, time_str);

    snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
             (unsigned long)(st.total_time_sec / 60),
             (unsigned long)(st.total_time_sec % 60));
    lv_label_set_text(s_lbl_time_tot, time_str);

    /* 4. 专辑封面：曲目变化时尝试解析内嵌封面，失败则回退黑胶唱盘 */
    if (st.state != PLAYER_STATE_STOPPED && st.current_path[0]) {
        if (strcmp(st.current_path, s_cover_path) != 0) {
            snprintf(s_cover_path, sizeof(s_cover_path), "%s", st.current_path);
            lv_img_dsc_t dsc;
            if (album_art_get(st.current_path, &dsc)) {
                s_cover_dsc = dsc;
                lv_img_set_src(s_img_cover, &s_cover_dsc);
                lv_obj_clear_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
                if (s_disc) lv_obj_add_flag(s_disc, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
                if (s_disc) lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        lv_obj_add_flag(s_img_cover, LV_OBJ_FLAG_HIDDEN);
        if (s_disc) lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_HIDDEN);
        s_cover_path[0] = 0;
    }
}
