/**
 * Host stub for ESP-IDF esp_lcd_panel_io.h — sim/ preview renderer only.
 */
#pragma once

#include "esp_lcd_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int dc_gpio_num;
    int cs_gpio_num;
    int sclk_gpio_num;
    int data_gpio_nums[8];
    unsigned int bus_width;
    unsigned int max_transfer_bytes;
    bool sio_mode;
    bool lsb_first;
} esp_lcd_panel_io_spi_config_t;

typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t panel_io,
                                                       esp_lcd_panel_io_event_data_t *edata,
                                                       void *user_ctx);

typedef struct {
    int lcd_cmd_bits;
    int lcd_param_bits;
} esp_lcd_panel_io_qspi_config_t;

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int lcd_cmd,
                                    const void *param, size_t param_size);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int lcd_cmd,
                                    const void *color, size_t color_size);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);

#ifdef __cplusplus
}
#endif
