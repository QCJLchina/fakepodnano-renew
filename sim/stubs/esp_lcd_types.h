/**
 * Host stub for ESP-IDF esp_lcd_types.h / esp_lcd_panel_io.h / esp_lcd_panel_ops.h.
 * The preview never talks to a real panel; these exist so ui_manager.h and any
 * view that includes it still compile.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
} esp_lcd_panel_io_event_data_t;

#ifdef __cplusplus
}
#endif
