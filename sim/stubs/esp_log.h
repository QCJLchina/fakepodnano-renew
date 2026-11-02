/**
 * Host stub for ESP-IDF esp_log.h — sim/ preview renderer only.
 * Log output goes to stderr so it never pollutes PNG generation.
 */
#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...);

#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...) \
    do { esp_log_write(level, tag, fmt, ##__VA_ARGS__); } while (0)

#define ESP_LOGE(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, len, level) do { (void)(tag); (void)(buf); (void)(len); (void)(level); } while (0)

#ifdef __cplusplus
}
#endif
