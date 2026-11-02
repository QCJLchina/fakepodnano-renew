/**
 * Host stub for FreeRTOS semphr.h — sim/ preview renderer only.
 * ui_lock()/ui_unlock() in the real firmware use a recursive mutex; in the
 * preview they are plain no-ops (sim/stubs/ui_manager_stub.cpp).
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t timeout);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

#ifdef __cplusplus
}
#endif
