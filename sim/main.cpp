/**
 * sim/main.cpp — HOST-SIDE LVGL preview renderer for FAKE_POD_NANO
 * ================================================================
 * Renders one UI view of the firmware at exactly 460x460 into a 24-bit BMP,
 * which sim/render.ps1 converts losslessly to PNG.
 *
 *   sim_preview <view> <out.bmp>
 *
 * Views: home | player | files | settings | uac | usbmode
 *
 * Design notes
 * ------------
 *  - The real UI sources are compiled unmodified (see sim/CMakeLists.txt);
 *    this file only provides lv_init + a memory display driver + a dump.
 *  - LV_COLOR_DEPTH / LV_COLOR_16_SWAP match the device (16 / 1).  The BMP
 *    writer reads lv_color_t.ch.{red,green,blue} instead of reinterpreting the
 *    raw 16-bit word, so the swapped byte order of the on-device framebuffer
 *    never leaks into the PNG.
 *  - Screenshots are deterministic except for the clock (time()) and the
 *    "is UAC streaming" sample data, which are both documented in README.md.
 */

#include "lvgl.h"

#include "ui_manager.h"
#include "ui_view_home.h"
#include "ui_view_player.h"
#include "ui_view_files.h"
#include "ui_view_settings.h"
#include "ui_view_uac.h"
#include "ui_view_usbmode.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define SIM_W 460
#define SIM_H 460

/* One full-screen RGB565 framebuffer (device resolution). */
static lv_color_t s_fb[SIM_W * SIM_H];
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_draw_buf_mem[SIM_W * SIM_H];

/* ------------------------------------------------------------------ display */

static void sim_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);

    for (int32_t y = 0; y < h; y++) {
        const int32_t dst_y = area->y1 + y;
        if (dst_y < 0 || dst_y >= SIM_H) continue;
        for (int32_t x = 0; x < w; x++) {
            const int32_t dst_x = area->x1 + x;
            if (dst_x < 0 || dst_x >= SIM_W) continue;
            s_fb[dst_y * SIM_W + dst_x] = color_p[y * w + x];
        }
    }

    lv_disp_flush_ready(drv);
}

/* ---------------------------------------------------------------- BMP output */
/* 24-bit uncompressed BMP, bottom-up rows. 460*3 = 1380 bytes per row, which is
 * already 4-byte aligned, so no row padding is needed. */

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void color_to_rgb888(lv_color_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
#if LV_COLOR_DEPTH == 16
    /* Read through LVGL's bitfields so both LV_COLOR_16_SWAP settings work.
     * With SWAP=1 (device default) green is split into green_h/green_l. */
    const uint16_t r5 = (uint16_t)c.ch.red;
  #if LV_COLOR_16_SWAP == 0
    const uint16_t g6 = (uint16_t)c.ch.green;
  #else
    const uint16_t g6 = (uint16_t)(((uint16_t)c.ch.green_h << 3) | (uint16_t)c.ch.green_l);
  #endif
    const uint16_t b5 = (uint16_t)c.ch.blue;
    *r = (uint8_t)((r5 * 255u + 15u) / 31u);
    *g = (uint8_t)((g6 * 255u + 31u) / 63u);
    *b = (uint8_t)((b5 * 255u + 15u) / 31u);
#elif LV_COLOR_DEPTH == 32
    *r = c.ch.red;
    *g = c.ch.green;
    *b = c.ch.blue;
#else
    *r = *g = *b = (uint8_t)c.full;
#endif
}

static bool write_bmp(const char *path)
{
    const uint32_t row_bytes = (uint32_t)SIM_W * 3u;
    const uint32_t image_bytes = row_bytes * (uint32_t)SIM_H;
    const uint32_t header_bytes = 54u;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[sim] cannot open output file: %s\n", path);
        return false;
    }

    uint8_t fh[14] = { 0 };
    fh[0] = 'B'; fh[1] = 'M';
    put_u32(&fh[2], header_bytes + image_bytes);
    put_u32(&fh[10], header_bytes);

    uint8_t ih[40] = { 0 };
    put_u32(&ih[0], 40);                 /* header size */
    put_u32(&ih[4], (uint32_t)SIM_W);    /* width */
    put_u32(&ih[8], (uint32_t)SIM_H);    /* height (positive = bottom-up) */
    put_u16(&ih[12], 1);                 /* planes */
    put_u16(&ih[14], 24);                /* bits per pixel */
    put_u32(&ih[16], 0);                 /* BI_RGB (uncompressed) */
    put_u32(&ih[20], image_bytes);
    put_u32(&ih[24], 2835);              /* ~72 DPI */
    put_u32(&ih[28], 2835);
    put_u32(&ih[32], 0);
    put_u32(&ih[36], 0);

    bool ok = fwrite(fh, 1, sizeof(fh), f) == sizeof(fh) &&
              fwrite(ih, 1, sizeof(ih), f) == sizeof(ih);

    uint8_t *row = (uint8_t *)malloc(row_bytes);
    if (!row) { fclose(f); return false; }

    for (int y = SIM_H - 1; ok && y >= 0; y--) {
        for (int x = 0; x < SIM_W; x++) {
            const lv_color_t c = s_fb[y * SIM_W + x];
            uint8_t r8 = 0, g8 = 0, b8 = 0;
            color_to_rgb888(c, &r8, &g8, &b8);
            row[x * 3 + 0] = b8; /* BMP stores BGR */
            row[x * 3 + 1] = g8;
            row[x * 3 + 2] = r8;
        }
        ok = fwrite(row, 1, row_bytes, f) == row_bytes;
    }

    free(row);
    fclose(f);
    if (!ok) fprintf(stderr, "[sim] write error: %s\n", path);
    return ok;
}

/* ------------------------------------------------------------------- views */

typedef struct {
    const char *name;
    lv_obj_t *(*create)(void);
    void (*update)(void);
} view_def_t;

static void view_none_update(void) {}

static const view_def_t kViews[] = {
    { "home",     ui_view_home_create,     ui_view_home_update },
    { "player",   ui_view_player_create,   ui_view_player_update },
    { "files",    ui_view_files_create,    ui_view_files_update },
    { "settings", ui_view_settings_create, ui_view_settings_update },
    { "uac",      ui_view_uac_create,      ui_view_uac_update },
    { "usbmode",  ui_view_usbmode_create,  ui_view_usbmode_update },
    { NULL, NULL, view_none_update },
};

static void print_usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s <view> <out.bmp>\n"
            "views: home player files settings uac usbmode about\n", argv0);
}

/* ------------------------------------------------------- "关于本设备" support */

/* Generated by sim/CMakeLists.txt: calls the UI's exported about screen when one
 * exists, otherwise returns NULL. */
extern "C" lv_obj_t *sim_about_create(void);
extern "C" void sim_about_update(void);

/* Heuristic fallback for views that keep the about screen as a hidden overlay:
 * find the first visible label whose text contains 关于, then send CLICKED to its
 * nearest clickable ancestor. Hidden subtrees are skipped, so the overlay itself
 * is never picked as the trigger. */
static bool sim_click_about_trigger(lv_obj_t *root)
{
    const uint32_t n = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        if (!child) continue;
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;

        if (lv_obj_check_type(child, &lv_label_class)) {
            const char *txt = lv_label_get_text(child);
            if (txt && strstr(txt, "关于") != NULL) {
                lv_obj_t *p = child;
                while (p && !lv_obj_has_flag(p, LV_OBJ_FLAG_CLICKABLE)) p = lv_obj_get_parent(p);
                if (p) {
                    lv_event_send(p, LV_EVENT_CLICKED, NULL);
                    fprintf(stderr, "[sim] clicked the 关于 trigger to open the about overlay\n");
                    return true;
                }
            }
        }
        if (sim_click_about_trigger(child)) return true;
    }
    return false;
}

static void sim_spin_frames(lv_disp_t *disp, const view_def_t *view, int frames, bool tick_update)
{
    for (int i = 0; i < frames; i++) {
        lv_tick_inc(10);
        if (tick_update && view && view->update && (i % 5) == 4) view->update();
        lv_timer_handler();
    }
    (void)disp;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 2;
    }
    const char *view_name = argv[1];
    const char *out_path = argv[2];

    const bool is_about = (strcmp(view_name, "about") == 0);

    const view_def_t *view = NULL;
    for (int i = 0; kViews[i].name; i++) {
        if (strcmp(kViews[i].name, view_name) == 0) view = &kViews[i];
    }
    if (!view && !is_about) {
        fprintf(stderr, "[sim] unknown view '%s'\n", view_name);
        print_usage(argv[0]);
        return 2;
    }

    lv_init();

    memset(s_fb, 0, sizeof(s_fb));
    lv_disp_draw_buf_init(&s_draw_buf, s_draw_buf_mem, NULL, SIM_W * SIM_H);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SIM_W;
    disp_drv.ver_res = SIM_H;
    disp_drv.flush_cb = sim_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (!disp) {
        fprintf(stderr, "[sim] lv_disp_drv_register failed\n");
        return 3;
    }

    /* The real ui_manager takes this lock before touching LVGL. */
    ui_lock(0);

    bool about_fallback = false;
    lv_obj_t *screen = NULL;

    if (is_about) {
        fprintf(stderr, "[sim] creating view 'about'\n");
        screen = sim_about_create();
        if (!screen) {
            fprintf(stderr,
                    "[sim] WARN: the UI does not export a 关于本设备 screen "
                    "(no no-arg ui_view_*about*_create in a header).\n"
                    "[sim] WARN: falling back to the settings view + clicking its 关于 trigger.\n");
            screen = ui_view_settings_create();
            about_fallback = true;
        }
        if (!screen) {
            fprintf(stderr, "[sim] settings view returned NULL; cannot render 'about'\n");
            return 4;
        }
    } else {
        fprintf(stderr, "[sim] creating view '%s'\n", view->name);
        screen = view->create();
        if (!screen) {
            fprintf(stderr, "[sim] view '%s' returned NULL screen\n", view->name);
            return 4;
        }
        /* The files view needs an explicit "enter" to populate its list from the
         * SD card; every other view has everything in create(). */
        if (strcmp(view->name, "files") == 0) {
            ui_view_files_enter();
        }
    }

    lv_scr_load(screen);
    lv_obj_update_layout(screen);

    /* Let timers/animations settle, then refresh the dynamic labels. */
    sim_spin_frames(disp, view, 60, true);

    if (is_about && about_fallback) {
        if (!sim_click_about_trigger(screen)) {
            fprintf(stderr,
                    "[sim] WARN: could not find a 关于 trigger; about.png is the "
                    "plain settings screen. Ask the settings owner to export "
                    "ui_view_settings_about_create(void) (no args) for a real about shot.\n");
        }
        sim_spin_frames(disp, view, 10, false);
    }

    if (is_about) {
        sim_about_update();
    } else if (view && view->update) {
        view->update();
    }

    /* Force one final full redraw so the dumped frame is the settled layout. */
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(disp);

    ui_unlock();

    if (!write_bmp(out_path)) return 5;

    fprintf(stdout, "rendered %s -> %s (%dx%d)\n", is_about ? "about" : view->name, out_path, SIM_W, SIM_H);
    return 0;
}
