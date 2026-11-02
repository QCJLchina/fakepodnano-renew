/**
 * Host stub for ESP-IDF esp_lcd_touch_cst820.h — sim/ preview renderer only.
 * The host renderer feeds no touch input; these declarations only exist so
 * sources that mention the touch driver still compile.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;
    uint8_t track_id;
} esp_lcd_touch_point_data_t;

typedef struct {
    uint16_t reset_gpio_num;
    uint16_t int_gpio_num;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    void *user_data;
} esp_lcd_touch_config_t;

typedef struct {
    void (*enter_sleep)(esp_lcd_touch_handle_t tp);
    void (*exit_sleep)(esp_lcd_touch_handle_t tp);
    void (*read_data)(esp_lcd_touch_handle_t tp);
    bool (*get_xy)(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                   uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
    esp_err_t (*get_xy_raw)(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                            uint8_t *point_num, uint8_t max_point_num);
    void (*reset)(esp_lcd_touch_handle_t tp);
} esp_lcd_touch_driver_t;

struct esp_lcd_touch_s {
    esp_lcd_touch_driver_t drv;
    uint16_t x[10];
    uint16_t y[10];
    uint16_t strength[10];
    uint8_t point_num;
    esp_lcd_touch_config_t cfg;
};

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                   uint16_t *strength, uint8_t *point_num,
                                   uint8_t max_point_num);
esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_touch_config_t *config,
                                       esp_lcd_touch_handle_t *tp);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_register_interrupt_callback(esp_lcd_touch_handle_t tp,
                                                    void (*callback)(esp_lcd_touch_handle_t));

#ifdef __cplusplus
}
#endif
