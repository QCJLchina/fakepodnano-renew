/**
 * Host stub for FreeRTOS task.h — sim/ preview renderer only.
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack_depth,
                       void *param, UBaseType_t prio, TaskHandle_t *handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack_depth,
                                   void *param, UBaseType_t prio, TaskHandle_t *handle,
                                   BaseType_t core_id);
void vTaskDelete(TaskHandle_t handle);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *prev_wake, TickType_t increment);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void taskYIELD(void);

#ifdef __cplusplus
}
#endif
