/**
 * Host stub for ESP-IDF esp_timer.h — sim/ preview renderer only.
 * Timers are recorded but never fire; the preview only needs the calls to link
 * and not crash. Field order matches ESP-IDF so C++20 designated initializers
 * written by the UI sources keep compiling.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);

typedef enum {
    ESP_TIMER_TASK,
    ESP_TIMER_ISR,
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;        /* Function to call */
    void *arg;                      /* Argument to pass to the callback */
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us);
bool esp_timer_is_active(esp_timer_handle_t timer);
int64_t esp_timer_get_time(void);
int64_t esp_timer_get_next_alarm(void);

#ifdef __cplusplus
}
#endif
