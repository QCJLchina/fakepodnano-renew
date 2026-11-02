/**
 * ui_manager_stub.cpp — replaces firmware/main/ui/ui_manager.cpp on the host.
 *
 * The real manager owns the LCD/SPI panel, the CST820 touch driver and the
 * LVGL task; none of that exists (or is wanted) in the preview. This stub only
 * implements the small API surface the views call, so they compile and run
 * unmodified. ui_switch_view() records the request instead of switching.
 */

#include "ui_manager.h"

#include <cstdio>
#include <cstring>

static ui_view_type_t s_current_view = UI_VIEW_HOME;
static int s_backlight_pct = 70;      /* design_ref/settings.png: 70% */
static int s_auto_off_sec = 30;       /* design_ref/settings.png: 30 秒 */
static bool s_touch_ok = true;

extern "C" esp_err_t ui_manager_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io)
{
    (void)panel;
    (void)io;
    return ESP_OK;
}

extern "C" void ui_switch_view(ui_view_type_t view)
{
    /* The preview renders one view per process; main.cpp decides what to load.
     * Recording is enough (and keeps click handlers harmless). */
    s_current_view = view;
}

extern "C" bool ui_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true; /* single-threaded host build: nothing to take */
}

extern "C" void ui_unlock(void) {}

extern "C" bool ui_touch_ok(void) { return s_touch_ok; }

extern "C" void ui_set_backlight_pct(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_backlight_pct = pct;
}

extern "C" int ui_get_backlight_pct(void) { return s_backlight_pct; }

extern "C" void ui_set_auto_off_sec(int sec) { s_auto_off_sec = sec; }
extern "C" int ui_get_auto_off_sec(void) { return s_auto_off_sec; }

extern "C" ui_view_type_t ui_get_current_view(void) { return s_current_view; }
