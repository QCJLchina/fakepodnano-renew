/**
 * esp_stubs.cpp — host implementations of the ESP-IDF / FreeRTOS API surface
 * used by the UI sources (all declared in sim/stubs/*.h).
 *
 * Everything here is deliberately inert: no threads, no timers, no reboot.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

/* ------------------------------------------------------------------ esp_err */
extern "C" const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
        case ESP_OK:                 return "ESP_OK";
        case ESP_FAIL:               return "ESP_FAIL";
        case ESP_ERR_NO_MEM:         return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:    return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:  return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:   return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:      return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:  return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:        return "ESP_ERR_TIMEOUT";
        case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
        case ESP_ERR_INVALID_CRC:    return "ESP_ERR_INVALID_CRC";
        case ESP_ERR_INVALID_VERSION:return "ESP_ERR_INVALID_VERSION";
        case ESP_ERR_INVALID_MAC:    return "ESP_ERR_INVALID_MAC";
        case ESP_ERR_NOT_FINISHED:   return "ESP_ERR_NOT_FINISHED";
        case ESP_ERR_NOT_ALLOWED:    return "ESP_ERR_NOT_ALLOWED";
        default:                     return "ESP_ERR_UNKNOWN";
    }
}

extern "C" const char *esp_err_to_name_r(esp_err_t code, char *buf, size_t buflen)
{
    if (buf && buflen) snprintf(buf, buflen, "%s", esp_err_to_name(code));
    return buf;
}

/* -------------------------------------------------------------------- esp_log */
extern "C" void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    static const char *lvl[] = { "N", "E", "W", "I", "D", "V" };
    int i = (int)level;
    if (i < 0 || i > 5) i = 3;
    fprintf(stderr, "[sim][%s][%s] ", lvl[i], tag ? tag : "-");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------------ esp_system */
static bool s_restart_requested = false;

extern "C" void esp_restart(void)
{
    s_restart_requested = true;
    fprintf(stderr, "[sim] esp_restart() requested - ignored in host preview\n");
}

extern "C" void esp_restart_noos(void) { esp_restart(); }

extern "C" bool sim_restart_requested(void) { return s_restart_requested; }

extern "C" uint32_t esp_get_free_heap_size(void) { return 8U * 1024U * 1024U; }
extern "C" uint32_t esp_get_minimum_free_heap_size(void) { return 4U * 1024U * 1024U; }
extern "C" uint32_t esp_random(void) { return 0x5EED1234U; }

/* ------------------------------------------------------------------- esp_timer */
struct esp_timer {
    esp_timer_cb_t cb;
    void *arg;
    bool active;
    bool periodic;
    int64_t period_us;
};

extern "C" int64_t esp_timer_get_time(void)
{
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return (int64_t)duration_cast<microseconds>(steady_clock::now() - t0).count();
}

extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!args || !out) return ESP_ERR_INVALID_ARG;
    esp_timer *t = (esp_timer *)calloc(1, sizeof(esp_timer));
    if (!t) return ESP_ERR_NO_MEM;
    t->cb = args->callback;
    t->arg = args->arg;
    *out = t;
    return ESP_OK;
}

extern "C" esp_err_t esp_timer_delete(esp_timer_handle_t t) { free(t); return ESP_OK; }

extern "C" esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    t->active = true;
    t->periodic = false;
    t->period_us = (int64_t)timeout_us;
    return ESP_OK; /* never fires during a preview run */
}

extern "C" esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    t->active = true;
    t->periodic = true;
    t->period_us = (int64_t)period_us;
    return ESP_OK;
}

extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t t)
{
    if (!t) return ESP_ERR_INVALID_ARG;
    t->active = false;
    return ESP_OK;
}

extern "C" esp_err_t esp_timer_restart(esp_timer_handle_t t, uint64_t timeout_us)
{
    return esp_timer_start_once(t, timeout_us);
}

extern "C" bool esp_timer_is_active(esp_timer_handle_t t) { return t && t->active; }
extern "C" int64_t esp_timer_get_next_alarm(void) { return INT64_MAX; }

/* --------------------------------------------------------------- esp_heap_caps */
extern "C" void *heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }
extern "C" void *heap_caps_calloc(size_t n, size_t size, uint32_t caps) { (void)caps; return calloc(n, size); }
extern "C" void *heap_caps_realloc(void *p, size_t size, uint32_t caps) { (void)caps; return realloc(p, size); }
extern "C" void heap_caps_free(void *p) { free(p); }
extern "C" size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 8U * 1024U * 1024U; }
extern "C" size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 4U * 1024U * 1024U; }

/* -------------------------------------------------------------------- FreeRTOS */
extern "C" BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack,
                                  void *param, UBaseType_t prio, TaskHandle_t *handle)
{
    (void)task; (void)name; (void)stack; (void)param; (void)prio;
    if (handle) *handle = (TaskHandle_t)1;
    return pdPASS;
}

extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                              void *param, UBaseType_t prio, TaskHandle_t *handle,
                                              BaseType_t core)
{
    (void)core;
    return xTaskCreate(task, name, stack, param, prio, handle);
}

extern "C" void vTaskDelete(TaskHandle_t h) { (void)h; }
extern "C" void vTaskDelay(TickType_t ticks) { (void)ticks; }
extern "C" void vTaskDelayUntil(TickType_t *prev, TickType_t inc) { if (prev) *prev += inc; }
extern "C" TickType_t xTaskGetTickCount(void) { return (TickType_t)(esp_timer_get_time() / 1000); }
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
extern "C" void taskYIELD(void) {}

extern "C" SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
extern "C" SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (SemaphoreHandle_t)1; }
extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return pdTRUE; }
extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
extern "C" BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return pdTRUE; }
extern "C" BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }
extern "C" void vSemaphoreDelete(SemaphoreHandle_t s) { (void)s; }
