/**
 * FAKE_POD_NANO 音乐目录页（TF 卡文件夹浏览）实现
 * ================================================
 * 视觉基准：prototype/index.html 的 `.nav-header / .file-path-header / .file-list /
 *           .file-item / .file-item-icon / .file-item-name / .file-item-sub / .folder-arrow`
 *           → design_ref/files.png（460x460 设计截图，2x）。
 *
 * 版式（页面内边距 18/14，子对象坐标从页面内容区起算，内容宽 424）：
 *   导航头   屏幕 y=14，高 36：左「返回」药丸（home 图标 + 文案）+ 标题「TF 卡音乐库」
 *   面包屑   屏幕 y=58：`路径: /sdcard/...`（前缀次级灰、路径强调色）
 *   列表     屏幕 y=80 起，424x366，行间距 8，行 424x62 圆角 14：
 *              左 34x34 图标底板（文件夹=强调色淡底+文件夹图标；音频=格式文字徽标）
 *              中 文件名（白，单行省略）+ 副标题（次级灰：目录=曲目数；音频=大小）
 *              右 文件夹=「N 首」+ ›   音频=文件大小
 *   手势条   底部居中（ui_gesture_hint_create）
 *
 * 行为保持与旧实现一致：
 *   - 第一遍列目录、第二遍列可播放音频；内部堆低于阈值就截断（避免把内部 RAM 吃光）；
 *   - 目录切换走「置标志 + 一次性 lv_timer」，不在事件分发中途重建整棵树；
 *   - 子目录返回上一级，根目录返回桌面；点击音频文件 player_play() + 切播放页。
 */

#include "ui_view_files.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "player_service.h"

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

static const char *TAG = "FILES";

#define FILE_ROOT       "/sdcard"
#define MAX_ENTRIES     160
#define ROW_W           424
#define ROW_H           62
#define ROW_ICON        34
#define ROW_ICON_R      9
#define ROW_GAP         8
#define ROW_PAD         14
#define TEXT_X          (ROW_PAD + ROW_ICON + 10)
#define LIST_Y          66   /* 页面内容区坐标：14(nav) + 36 + 4 + 16(breadcrumb) + 6 */

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_lbl_path = NULL;
static lv_obj_t *s_lbl_back = NULL;      /* 返回药丸里的文案，随层级切换「桌面/上一级」 */
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

static void ext_upper(const char *name, char *out, size_t out_len)
{
    out[0] = '\0';
    const char *dot = strrchr(name, '.');
    if (!dot) return;
    snprintf(out, out_len, "%s", dot + 1);
    for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
}

static void fmt_size(uint64_t bytes, char *out, size_t out_len)
{
    if (bytes >= (uint64_t)1024 * 1024) {
        snprintf(out, out_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, out_len, "%u KB", (unsigned)((bytes + 1023) / 1024));
    }
}

/** 纯色圆角底板（本页用于 34x34 图标位；不走渐变） */
static lv_obj_t* flat_tile(lv_obj_t *parent, int x, int y, int size, int radius, lv_color_t bg)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(t, size, size);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_style_bg_color(t, bg, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, radius, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    return t;
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
    lv_obj_t *lbl = ui_label_create(s_list, text, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(lbl, ROW_W);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(lbl, 18, 0);
}

static void files_refresh(void);

/* 目录切换延后到事件返回后再重建列表（沿用旧实现的理由：触摸回调里直接
 * lv_obj_clean() + 重建上百行会把对象树压进较浅的 LVGL 任务栈，而且在事件
 * 分发中途删掉事件目标对象）。 */
static volatile bool s_refresh_pending = false;

static void refresh_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    s_refresh_pending = false;
    files_refresh();
}

static void request_refresh(void)
{
    if (s_refresh_pending) return;
    s_refresh_pending = true;
    if (!lv_timer_create(refresh_timer_cb, 30, NULL)) {
        s_refresh_pending = false;
        files_refresh();   /* 建 timer 失败就退回同步路径 */
    }
}

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
        ESP_LOGI(TAG, "进入目录: %s", ctx->path);
        snprintf(s_path, sizeof(s_path), "%s", ctx->path);
        request_refresh();
    } else {
        ESP_LOGI(TAG, "播放文件: %s", ctx->path);
        player_play(ctx->path);
        ui_switch_view(UI_VIEW_PLAYER);
    }
}

static void row_click_cb(lv_event_t *e);

/* ---------------------------------------------------------------
 * 一行文件/文件夹
 * --------------------------------------------------------------- */
static void add_row(const char *name, const char *full_path, bool is_dir, uint64_t size_bytes)
{
    lv_obj_t *row = ui_surface_create(s_list, 0, 0, ROW_W, ROW_H, 14);

    row_ctx_t *ctx = (row_ctx_t *)calloc(1, sizeof(row_ctx_t));
    if (ctx) {
        ctx->path = strdup(full_path);
        ctx->is_dir = is_dir;
        lv_obj_set_user_data(row, ctx);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, ctx);
    }

    /* 左侧 34x34 图标位 */
    if (is_dir) {
        lv_obj_t *tile = flat_tile(row, ROW_PAD, (ROW_H - ROW_ICON) / 2, ROW_ICON, ROW_ICON_R,
                                   UI_COL_ACCENT_SOFT);
        lv_obj_t *img = ui_icon_create(tile, 0, 0, &ui_icon_folder, UI_COL_ACCENT);
        lv_obj_center(img);
    } else {
        lv_obj_t *tile = flat_tile(row, ROW_PAD, (ROW_H - ROW_ICON) / 2, ROW_ICON, ROW_ICON_R,
                                   UI_COL_PILL);
        char ext[8];
        ext_upper(name, ext, sizeof(ext));
        lv_obj_t *tag = ui_label_create(tile, ext, UI_FONT_TINY, UI_COL_ACCENT);
        lv_obj_center(tag);
    }

    /* 中间：文件名 + 副标题 */
    lv_obj_t *nm = ui_label_create(row, name, UI_FONT_CJK_S, UI_COL_TEXT);
    lv_obj_set_width(nm, ROW_W - TEXT_X - 96);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(nm, TEXT_X, 9);

    char sub[32] = { 0 };
    if (is_dir) {
        int cnt = count_tracks_in(full_path);
        snprintf(sub, sizeof(sub), "%d 首曲目", cnt);
    } else if (size_bytes > 0) {
        fmt_size(size_bytes, sub, sizeof(sub));
    } else {
        snprintf(sub, sizeof(sub), "音频文件");
    }
    lv_obj_t *subl = ui_label_create(row, sub, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(subl, ROW_W - TEXT_X - 96);
    lv_label_set_long_mode(subl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(subl, TEXT_X, 34);

    /* 右侧 */
    if (is_dir) {
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d 首", count_tracks_in(full_path));
        lv_obj_t *cl = ui_label_create(row, cnt, UI_FONT_CJK_S, UI_COL_TEXT2);
        lv_obj_align(cl, LV_ALIGN_RIGHT_MID, -30, 0);

        lv_obj_t *arrow = ui_label_create(row, LV_SYMBOL_RIGHT, UI_FONT_SM, UI_COL_TEXT3);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
    } else if (size_bytes > 0) {
        char sz[24];
        fmt_size(size_bytes, sz, sizeof(sz));
        lv_obj_t *sl = ui_label_create(row, sz, UI_FONT_CJK_S, UI_COL_TEXT2);
        lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -14, 0);
    }
}

/* ---------------------------------------------------------------
 * 刷新列表
 * --------------------------------------------------------------- */
static void files_refresh(void)
{
    if (!s_screen || !s_list) return;

    lv_label_set_text(s_lbl_path, s_path);
    if (s_lbl_back) {
        lv_label_set_text(s_lbl_back, strcmp(s_path, FILE_ROOT) == 0 ? "桌面" : "上一级");
    }
    free_rows();

    ESP_LOGI(TAG, "刷新目录: %s | 堆 内部%uKB 总%uKB",
             s_path,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(esp_get_free_heap_size() / 1024));

    DIR *dir = opendir(s_path);
    if (!dir) {
        add_info_row("无法打开目录（TF 卡未挂载？）");
        return;
    }

    int count = 0;
    bool truncated = false;
    struct dirent *ent;

    /* 内部 RAM 低于该阈值就停止建行：每行 4 个 LVGL 对象约 1KB（走内部堆），
     * 把内部堆吃光会让 LCD 的 DMA 事务、USB 等分配失败 → 花屏/重启。 */
    const size_t MIN_INTERNAL_HEAP = 48 * 1024;

    /* 第一遍：目录 */
    while ((ent = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", s_path, ent->d_name);
        struct stat st;
        bool isdir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        if (!isdir) continue;
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_HEAP) { truncated = true; break; }
        add_row(ent->d_name, full, true, 0);
        count++;
    }

    /* 第二遍：可播放音频文件 */
    rewinddir(dir);
    while (!truncated && (ent = readdir(dir)) != NULL && count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", s_path, ent->d_name);
        struct stat st;
        bool isdir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        if (isdir) continue;
        if (!ext_is_playable(ent->d_name)) continue;
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_INTERNAL_HEAP) { truncated = true; break; }
        add_row(ent->d_name, full, false, (stat(full, &st) == 0) ? (uint64_t)st.st_size : 0);
        count++;
    }
    closedir(dir);

    if (count == 0) {
        add_info_row("（空目录 / 没有可播放的音频）");
    } else if (truncated) {
        add_info_row("（文件较多或内存不足，仅显示部分）");
    }

    ESP_LOGI(TAG, "列表完成: %d 项 | 堆 内部%uKB 总%uKB",
             count,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(esp_get_free_heap_size() / 1024));
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
    ESP_LOGI(TAG, "返回上级: %s", s_path);
    request_refresh();
}

/* ---------------------------------------------------------------
 * 创建页面
 * --------------------------------------------------------------- */
lv_obj_t* ui_view_files_create(void)
{
    s_screen = ui_screen_create();
    lv_obj_t *page = ui_page_create(s_screen);

    /* 导航头：屏幕 y=14 */
    lv_obj_t *nav = ui_nav_header_create(page, 14, "桌面", "TF 卡音乐库", back_click_cb, NULL);
    /* 取返回药丸里的文案 label（row -> pill -> [home 图标, 文字]）以便切换文案 */
    lv_obj_t *pill = lv_obj_get_child(nav, 0);
    if (pill) s_lbl_back = lv_obj_get_child(pill, 1);

    /* 面包屑：屏幕 y=58（页面内容区 y=44） */
    lv_obj_t *pre = ui_label_create(page, "路径:", UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_pos(pre, 0, 44);
    s_lbl_path = ui_label_create(page, FILE_ROOT, UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_set_width(s_lbl_path, ROW_W - 34);
    lv_label_set_long_mode(s_lbl_path, LV_LABEL_LONG_DOT);
    lv_obj_align_to(s_lbl_path, pre, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    /* 列表：屏幕 y=80（页面内容区 y=66），424x366，行间距 8，可上下滚动 */
    s_list = ui_scroll_column_create(page, 0, LIST_Y, ROW_W, 366, ROW_GAP);

    ui_gesture_hint_create(s_screen);

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
