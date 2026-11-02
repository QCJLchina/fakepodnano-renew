/**
 * Host stub for ESP-IDF esp_lcd_panel_ops.h — sim/ preview renderer only.
 * Only the symbols ui_manager.cpp references are declared; nothing here draws.
 */
#pragma once

#include "esp_lcd_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int reset_gpio_num;
    bool reset_level;
    int init_cmds_size;
} esp_lcd_panel_dev_config_t;

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start,
                                    int x_end, int y_end, const void *color_data);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool mirror_x, bool mirror_y);
esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool swap_axes);
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t panel, int x_gap, int y_gap);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert_color_data);

#ifdef __cplusplus
}
#endif
