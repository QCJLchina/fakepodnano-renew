/**
 * FAKE_POD_NANO 音乐目录页（TF 卡文件夹浏览）实现
 */

#include "ui_view_files.h"
#include "ui_manager.h"
#include "ui_fonts.h"
#include "player_service.h"

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define FILE_ROOT       "/sdcard"
#define MAX_ENTRIES     160
#define ROW_H           54

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_lbl_path = NULL;
static char s_path[256] = FILE_ROOT;

typedef struct {
    char *path;
    bool  is_dir;
} row_ctx_t;

/* ---------------------------------------------------------------
 * 小工具
 * --------------------------------------------------------------- */
static bool ext_is_playable(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    char e[8];
    size_t n = strlen(ext);
    if (n >= sizeof(e)) return false;
    for (size_t i = 0; i <= n; i++) e[i] = (char)tolower((unsigned char)ext[i]);
    return !strcmp(e, ".wav") || !strcmp(e, ".flac") ||
           !strcmp(e, ".mp3") || !strcmp(e, ".aac") || !strcmp(e, ".m4a") ||
           !strcmp(e, ".dsf") || !strcmp(e, ".dsd");
}

static void free_rows(void)
{
    if (!s_list) return;
    uint32_t n = lv_obj_get_child_cnt(s_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(s_list, i);
        row_ctx_t *ctx = (row_ctx_t *)lv_obj_get_user_data(row);
        if (ctx) {
            free(ctx->path);
            free(ctx);
            lv_obj_set_user_data(row, NULL);
        }
    }
    lv_obj_clean(s_list);
}

static void add_info_row(const char *text)
{
    lv_obj_t *lbl = lv_label_create(s_list);
    lv_obj_set_style_text_font(lbl, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(130, 130, 145), 0);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_pad_all(lbl, 10, 0);
}

static void files_refresh(void);

/* 统计某目录下的可播放音频文件数量（仅本层） */
static int count_tracks_in(const char *dirpath)
{
    DIR *d = opendir(dirpath);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (ext_is_playable(e->d_name)) n++;
    }
    closedir(d);
    return n;
}

static void row_click_cb(lv_event_t *e)
{
    row_ctx_t *ctx = (row_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    if (ctx->is_dir) {
        snprintf(s_path, sizeof(s_path), "%s", ctx->path);
        files_refresh();
    } else {
        player_play(ctx->path);
        ui_switch_view(UI_VIEW_PLAYER);
    }
}

static void add_row(const char *name, const char *full_path, bool is_dir)
{
    lv_obj_t *row = lv_btn_create(s_list);
    lv_obj_set_size(row, 430, ROW_H);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, lv_color_make(20, 20, 26), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_make(44, 44, 54), 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 8, 0);

    row_ctx_t *ctx = (row_ctx_t *)calloc(1, sizeof(row_ctx_t));
    if (ctx) {
        ctx->path = strdup(full_path);
        ctx->is_dir = is_dir;
        lv_obj_set_user_data(row, ctx);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, ctx);
    }

    lv_obj_t *ico = lv_label_create(row);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ico, lv_palette_main(is_dir ? LV_PALETTE_AMBER : LV_PALETTE_CYAN), 0);
    lv_label_set_text(ico, is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *nm = lv_label_create(row);
    lv_obj_set_style_text_font(nm, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(nm, lv_color_white(), 0);
    lv_obj_set_width(nm, is_dir ? 340 : 300);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_label_set_text(nm, name);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 40, 0);

    if (!is_dir) {
        lv_obj_t *tag = lv_label_create(row);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tag, lv_color_make(150, 150, 165), 0);
        const char *dot = strrchr(name, '.');
        char ext[8] = "AUDIO";
        if (dot) {
            snprintf(ext, sizeof(ext), "%s", dot + 1);
            for (char *p = ext; *p; p++) *p = (char)toupper((unsigned char)*p);
        }
        lv_label_set_text(tag, ext);
        lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -6, 0);
    } else {
        lv_obj_t *arrow = lv_label_create(row);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(arrow, lv_color_make(120, 120, 135), 0);
        lv_label_set_text(arrow, ">");
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -6, 0);

        /* 该文件夹内可播放曲目数量 */
        int cnt = count_tracks_in(full_path);
        char cb[24];
        snprintf(cb, sizeof(cb), "%d 首", cnt);
        lv_obj_t *cntlbl = lv_label_create(row);
        lv_obj_set_style_text_font(cntlbl, &ui_font_cjk_16, 0);
        lv_obj_set_style_text_color(cntlbl, lv_color_make(150, 150, 165), 0);
        lv_label_set_text(cntlbl, cb);
        lv_obj_align(cntlbl, LV_ALIGN_RIGHT_MID, -30, 0);
    }
}

/* ---------------------------------------------------------------
 * 刷新列表：先目录后文件
 * --------------------------------------------------------------- */
static void files_refresh(void)
{
    if (!s_screen || !s_list) return;
    lv_label_set_text(s_lbl_path, s_path);
    free_rows();

    DIR *dir = opendir(s_path);
    if (!dir) {
        add_info_row("无法打开目录（TF 卡未挂载？）");
        return;
    }

    int count = 0;
    struct dirent *ent;

    /* 第一遍：目录 */
    while ((ent = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", s_path, ent->d_name);
        struct stat st;
        bool isdir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        if (!isdir) continue;
        add_row(ent->d_name, full, true);
        count++;
    }

    /* 第二遍：可播放音频文件 */
    rewinddir(dir);
    while ((ent = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", s_path, ent->d_name);
        struct stat st;
        bool isdir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        if (isdir) continue;
        if (!ext_is_playable(ent->d_name)) continue;
        add_row(ent->d_name, full, false);
        count++;
    }
    closedir(dir);

    if (count == 0) {
        add_info_row("（空目录 / 没有可播放的音频）");
    }
}

/* ---------------------------------------------------------------
 * 返回：子目录 → 上一级；根目录 → 桌面主页
 * --------------------------------------------------------------- */
static void back_click_cb(lv_event_t *e)
{
    (void)e;
    if (strcmp(s_path, FILE_ROOT) == 0) {
        ui_switch_view(UI_VIEW_HOME);
        return;
    }
    char *slash = strrchr(s_path, '/');
    if (slash && slash != s_path) {
        *slash = '\0';
    } else {
        snprintf(s_path, sizeof(s_path), "%s", FILE_ROOT);
    }
    files_refresh();
}

/* ---------------------------------------------------------------
 * 创建页面
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_files_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* 顶部：返回键 + 面包屑路径 */
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 60, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_radius(btn_back, 10, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(28, 28, 34), 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_back, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    s_lbl_path = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_path, &ui_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_lbl_path, lv_color_make(160, 160, 175), 0);
    lv_obj_set_width(s_lbl_path, 360);
    lv_label_set_long_mode(s_lbl_path, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_lbl_path, FILE_ROOT);
    lv_obj_align(s_lbl_path, LV_ALIGN_TOP_LEFT, 82, 18);

    /* 列表容器 */
    s_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_list, 440, 380);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    return s_screen;
}

void ui_view_files_enter(void)
{
    snprintf(s_path, sizeof(s_path), "%s", FILE_ROOT);
    files_refresh();
}

void ui_view_files_update(void)
{
    /* 静态列表，无需周期刷新 */
}
