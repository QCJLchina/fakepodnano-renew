/**
 * FAKE_POD_NANO 本地音频播放服务实现
 */

#include "player_service.h"
#include "audio_pipeline.h"
#include "decoder_base.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "PLAYER_SVC";

#define PCM_CHUNK_BYTES (4096)

static player_status_t s_status = {
    .state = PLAYER_STATE_STOPPED,
    .volume = 80,
};

static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;
static FILE *s_current_file = NULL;
static AudioDecoder *s_decoder = NULL;
static bool s_req_stop = false;
static bool s_req_new_track = false;
static char s_next_path[256] = {0};

/* ---------------------------------------------------------------
 * 核心后台播放线程
 * --------------------------------------------------------------- */
static void player_task(void *arg)
{
    ESP_LOGI(TAG, "播放服务线程已启动");

    /* 在 PSRAM 中分配 PCM 搬运临时缓冲（4KB） */
    uint8_t *pcm_buf = (uint8_t*)heap_caps_malloc(PCM_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm_buf) {
        pcm_buf = (uint8_t*)malloc(PCM_CHUNK_BYTES);
    }

    while (1) {
        /* 1. 检查是否有新曲目播放请求 */
        if (s_req_new_track) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_req_new_track = false;

            /* 清理上一曲 */
            if (s_decoder) {
                s_decoder->close();
                delete s_decoder;
                s_decoder = NULL;
            }
            if (s_current_file) {
                fclose(s_current_file);
                s_current_file = NULL;
            }

            /* 打开新文件 */
            s_current_file = fopen(s_next_path, "rb");
            if (s_current_file) {
                s_decoder = audio_decoder_create_for_file(s_next_path, s_current_file);
                if (s_decoder) {
                    snprintf(s_status.current_path, sizeof(s_status.current_path), "%s", s_next_path);
                    s_status.track_info = *s_decoder->get_info();
                    s_status.total_time_sec = s_status.track_info.total_time_sec;
                    s_status.current_time_sec = 0;
                    s_status.state = PLAYER_STATE_PLAYING;

                    /* 动态通知音频硬件输出重配采样率与位深 */
                    audio_pipeline_reconfig(&s_status.track_info.fmt);
                    ESP_LOGI(TAG, "开始播放: %s (格式: %lu Hz, %d bit, %d 声道)",
                             s_status.current_path,
                             (unsigned long)s_status.track_info.fmt.sample_rate,
                             s_status.track_info.fmt.bits_per_sample,
                             s_status.track_info.fmt.channels);
                } else {
                    ESP_LOGE(TAG, "创建解码器失败: %s", s_next_path);
                    fclose(s_current_file);
                    s_current_file = NULL;
                    s_status.state = PLAYER_STATE_ERROR;
                }
            } else {
                ESP_LOGE(TAG, "无法打开音频文件: %s", s_next_path);
                s_status.state = PLAYER_STATE_ERROR;
            }
            xSemaphoreGive(s_mutex);
        }

        /* 2. 检查停止请求 */
        if (s_req_stop) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_req_stop = false;
            if (s_decoder) {
                s_decoder->close();
                delete s_decoder;
                s_decoder = NULL;
            }
            if (s_current_file) {
                fclose(s_current_file);
                s_current_file = NULL;
            }
            s_status.state = PLAYER_STATE_STOPPED;
            audio_pipeline_flush();
            xSemaphoreGive(s_mutex);
        }

        /* 3. 正常播放循环 */
        if (s_status.state == PLAYER_STATE_PLAYING && s_decoder) {
            size_t out_bytes = 0;
            bool ok = s_decoder->decode_frame(pcm_buf, PCM_CHUNK_BYTES, &out_bytes);

            if (ok && out_bytes > 0) {
                size_t written = 0;
                audio_pipeline_write(pcm_buf, out_bytes, &written, 200);

                const audio_track_info_t *info = s_decoder->get_info();
                if (info) {
                    s_status.current_time_sec = info->current_time_sec;
                }
            } else if (!ok) {
                /* 播放结束 */
                ESP_LOGI(TAG, "曲目播放完成: %s", s_status.current_path);
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_status.state = PLAYER_STATE_COMPLETED;
                if (s_decoder) {
                    s_decoder->close();
                    delete s_decoder;
                    s_decoder = NULL;
                }
                if (s_current_file) {
                    fclose(s_current_file);
                    s_current_file = NULL;
                }
                xSemaphoreGive(s_mutex);
            }
        } else {
            /* 暂停或停止状态下休眠等待 */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ---------------------------------------------------------------
 * 公共控制 API
 * --------------------------------------------------------------- */
esp_err_t player_service_init(void)
{
    if (s_task_handle) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* 创建音频解码专用任务，分配 8KB 任务栈，运行于 Core 1，优先级高 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        player_task,
        "player_task",
        8192,
        NULL,
        5,  /* 高于普通任务 */
        &s_task_handle,
        1   /* CPU 1 */
    );

    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t player_play(const char *filepath)
{
    if (!filepath || !s_task_handle) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(s_next_path, sizeof(s_next_path), "%s", filepath);
    s_req_new_track = true;
    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t player_pause(void)
{
    if (!s_task_handle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_status.state == PLAYER_STATE_PLAYING) {
        s_status.state = PLAYER_STATE_PAUSED;
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t player_resume(void)
{
    if (!s_task_handle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_status.state == PLAYER_STATE_PAUSED) {
        s_status.state = PLAYER_STATE_PLAYING;
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t player_stop(void)
{
    if (!s_task_handle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_req_stop = true;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t player_seek(uint32_t sec)
{
    if (!s_task_handle) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_decoder) {
        s_decoder->seek(sec);
        s_status.current_time_sec = sec;
    }
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t player_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_status.volume = vol;
    return audio_pipeline_set_volume(vol);
}

void player_get_status(player_status_t *out_status)
{
    if (!out_status) return;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out_status = s_status;
    if (s_mutex) xSemaphoreGive(s_mutex);
}
