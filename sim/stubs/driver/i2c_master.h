/**
 * Host stub for ESP-IDF driver/i2c_master.h — sim/ preview renderer only.
 * dac_cs43131.h includes this for its handle types; the preview never talks I2C.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;

typedef enum {
    I2C_CLK_SRC_DEFAULT = 0,
} i2c_clock_source_t;

typedef struct {
    i2c_master_bus_handle_t dev_handle;
} i2c_master_bus_config_t_compat;

typedef struct {
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint8_t device_addr_length;
} i2c_device_config_t;

typedef struct {
    int sda_io_num;
    int scl_io_num;
    uint32_t scl_speed_hz;
} i2c_master_bus_config_min_t;

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t handle, const uint8_t *write_buffer,
                              size_t write_size, int xfer_timeout_ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t handle, uint8_t *read_buffer,
                             size_t read_size, int xfer_timeout_ms);

#ifdef __cplusplus
}
#endif
