/**
 * Host stub for ESP-IDF esp_system.h — sim/ preview renderer only.
 * esp_restart() must never kill the harness while rendering, so it just
 * records the request and returns.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void esp_restart(void);
void esp_restart_noos(void);

uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
uint32_t esp_random(void);

/* true once esp_restart() was requested (used by main.cpp only) */
bool sim_restart_requested(void);

#ifdef __cplusplus
}
#endif
