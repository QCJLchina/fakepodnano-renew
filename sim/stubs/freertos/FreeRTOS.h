/**
 * Host stub for FreeRTOS.h — sim/ preview renderer only.
 * Single-threaded host build: tasks are not created, delays are no-ops.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t StackType_t;

#define portTICK_PERIOD_MS      1
#define configTICK_RATE_HZ      1000
#define pdMS_TO_TICKS(ms)       ((TickType_t)(ms))
#define portMAX_DELAY           ((TickType_t)0xffffffffU)
#define portTICK_RATE_MS        portTICK_PERIOD_MS

#define pdTRUE                  ((BaseType_t)1)
#define pdFALSE                 ((BaseType_t)0)
#define pdPASS                  pdTRUE
#define pdFAIL                  pdFALSE

#define tskNO_AFFINITY          0x7FFFFFFF

#ifdef __cplusplus
}
#endif
