/**
 * FAKE_POD_NANO 本地音频播放服务实现
 */

#include "player_service.h"
#include "audio_pipeline.h"
#include "decoder_base.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <dirent.h>
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
static bool s_req_seek = false;
static uint32_t s_seek_sec = 0;
static bool s_output_held = false;
static char s_next_path[256] = {0};

static void player_release_output(void)
{
    if (s_output_held) {
        audio_pipeline_release(AUDIO_OWNER_PLAYER);
        s_output_held = false;
    }
}

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
            player_release_output();
            s_req_seek = false;

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

                    if (audio_pipeline_acquire(AUDIO_OWNER_PLAYER) != ESP_OK) {
                        ESP_LOGW(TAG, "I2S 正被 USB 声卡占用，拒绝本地播放");
                        s_decoder->close();
                        delete s_decoder;
                        s_decoder = NULL;
                        fclose(s_current_file);
                        s_current_file = NULL;
                        s_status.state = PLAYER_STATE_ERROR;
                        xSemaphoreGive(s_mutex);
                        continue;
                    }
                    s_output_held = true;
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
            s_req_seek = false;
            s_status.state = PLAYER_STATE_STOPPED;
            player_release_output();
            audio_pipeline_flush();
            xSemaphoreGive(s_mutex);
        }

        /* 跳转只在本任务里做，避免和 decode_frame 同时操作同一个 FILE */
        if (s_req_seek && s_decoder && s_status.state == PLAYER_STATE_PLAYING) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            uint32_t sec = s_seek_sec;
            s_req_seek = false;
            xSemaphoreGive(s_mutex);
            if (s_decoder->seek(sec)) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_status.current_time_sec = sec;
                xSemaphoreGive(s_mutex);
            }
        }

        /* 3. 正常播放循环 */
        if (s_status.state == PLAYER_STATE_PLAYING && s_decoder) {
            size_t out_bytes = 0;
            bool ok = s_decoder->decode_frame(pcm_buf, PCM_CHUNK_BYTES, &out_bytes);

            if (ok && out_bytes > 0) {
                size_t written = 0;
                audio_pipeline_write(pcm_buf, out_bytes, &written, 200);

                const audio_track_info_t *info = s_decoder->get_info();
                if (info && s_mutex && xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                    s_status.current_time_sec = info->current_time_sec;
                    xSemaphoreGive(s_mutex);
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
                player_release_output();
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

    /* 解码暂存已放到堆上；12KB 覆盖 fopen/解码调用链，避免再把整帧 PCM 放回栈 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        player_task,
        "player_task",
        12288,
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

/* ---------------------------------------------------------------
 * 上一首 / 下一首：扫描当前曲目所在目录内的可播放文件，循环切换
 * --------------------------------------------------------------- */
static bool svc_name_playable(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    char e[8];
    size_t n = strlen(ext);
    if (n >= sizeof(e)) return false;
    for (size_t i = 0; i <= n; i++) e[i] = (char)tolower((unsigned char)ext[i]);
    return !strcmp(e, ".wav") || !strcmp(e, ".flac") ||
           !strcmp(e, ".mp3") || !strcmp(e, ".aac") || !strcmp(e, ".m4a") ||
           !strcmp(e, ".dsf") || !strcmp(e, ".dsd");
}

static int svc_name_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static esp_err_t play_sibling(int step)
{
    if (!s_task_handle) return ESP_ERR_INVALID_STATE;

    char cur[256];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(cur, sizeof(cur), "%s", s_status.current_path);
    xSemaphoreGive(s_mutex);
    if (cur[0] == '\0') return ESP_ERR_INVALID_STATE;

    const char *slash = strrchr(cur, '/');
    if (!slash) return ESP_ERR_INVALID_STATE;
    char dir[256];
    size_t dl = (size_t)(slash - cur);
    if (dl == 0 || dl >= sizeof(dir)) return ESP_ERR_INVALID_STATE;
    memcpy(dir, cur, dl);
    dir[dl] = '\0';
    const char *base = slash + 1;

    DIR *dp = opendir(dir);
    if (!dp) return ESP_FAIL;

    const int max_tracks = 200;
    const int name_len = 128;
    char *names = (char *)malloc((size_t)max_tracks * name_len);
    if (!names) {
        closedir(dp);
        return ESP_ERR_NO_MEM;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && count < max_tracks) {
        if (ent->d_name[0] == '.') continue;
        if (!svc_name_playable(ent->d_name)) continue;
        strncpy(names + count * name_len, ent->d_name, name_len - 1);
        names[count * name_len + (name_len - 1)] = '\0';
        count++;
    }
    closedir(dp);

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (count > 0) {
        qsort(names, (size_t)count, name_len, svc_name_cmp);
        int idx = -1;
        for (int i = 0; i < count; i++) {
            if (!strcmp(names + i * name_len, base)) { idx = i; break; }
        }
        if (idx < 0) idx = 0;
        int ni = (idx + step + count) % count;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir, names + ni * name_len);
        ret = player_play(full);
    }

    free(names);
    return ret;
}

esp_err_t player_next(void)
{
    return play_sibling(+1);
}

esp_err_t player_prev(void)
{
    return play_sibling(-1);
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
    s_seek_sec = sec;
    s_req_seek = true;
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
