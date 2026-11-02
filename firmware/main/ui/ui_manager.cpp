/**
 * FAKE_POD_NANO 全新 UI 管理器实现 (LVGL 8.3+)
 */

#include "ui_manager.h"
#include "ui_view_player.h"
#include "ui_view_uac.h"
#include "ui_view_home.h"
#include "ui_view_files.h"
#include "ui_view_settings.h"
#include "ui_view_usbmode.h"
#include "usb_uac_service.h"
#include "esp_lcd_panel_ops.h"
#include "esp_system.h"
#include "lcd_co5300.h"
#include "board_config.h"
#include "board.h"
#include "esp_lcd_touch_cst820.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

static const char *TAG = "UI_MGR";

#define LVGL_BUF_LINES  (46) /* 1/10 屏幕高度缓冲 */

static SemaphoreHandle_t s_lvgl_mux = NULL;
static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_disp_buf;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static bool s_touch_ok = false;
static uint32_t s_flush_timeout_cnt = 0;
static bool s_flush_needs_te = true;   /* 本刷新周期的第一块数据是否还等着对齐 TE */

static lv_obj_t *s_screen_home = NULL;
static lv_obj_t *s_screen_player = NULL;
static lv_obj_t *s_screen_uac = NULL;
static lv_obj_t *s_screen_files = NULL;
static lv_obj_t *s_screen_settings = NULL;
static lv_obj_t *s_screen_usb_mode = NULL;
static ui_view_type_t s_current_view = UI_VIEW_HOME;

/* 背光 / 自动息屏状态 */
static int s_bright_pct = 100;
static int s_auto_off_sec = 0;
static int64_t s_last_activity_us = 0;
static bool s_disp_on = true;

/* ---------------------------------------------------------------
 * 线程安全保护
 * --------------------------------------------------------------- */
bool ui_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_mux) return false;
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE;
}

void ui_unlock(void)
{
    if (s_lvgl_mux) {
        xSemaphoreGive(s_lvgl_mux);
    }
}

bool ui_touch_ok(void)
{
    return s_touch_ok;
}

void ui_set_backlight_pct(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bright_pct = pct;
    if (s_panel_handle) {
        lcd_set_brightness(s_panel_handle, (uint8_t)(pct * 255 / 100));
    }
}

int ui_get_backlight_pct(void)
{
    return s_bright_pct;
}

void ui_set_auto_off_sec(int sec)
{
    s_auto_off_sec = sec;
    s_last_activity_us = esp_timer_get_time();
}

int ui_get_auto_off_sec(void)
{
    return s_auto_off_sec;
}

ui_view_type_t ui_get_current_view(void)
{
    return s_current_view;
}

/* ---------------------------------------------------------------
 * LVGL 驱动回调对接
 * --------------------------------------------------------------- */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* 每个刷新周期只在【第一块】数据写 GRAM 之前对齐一次 vblank。
     * 放在这里而不是 render_start_cb，是因为真正写显存的时刻才是撕裂的成因：
     * 先把整帧渲染到 RAM（不碰面板），再在 TE 上升沿开始 DMA，撕裂最小。 */
    if (s_flush_needs_te) {
        s_flush_needs_te = false;
        if (lcd_te_sync_enabled()) {
            lcd_wait_te(LCD_TE_TIMEOUT_MS);
        }
    }

    /* 同步提交：必须等这块缓冲区的 QSPI DMA 真正传完，才能告诉 LVGL「这块可以复用了」。
     * 旧写法是 draw_bitmap() 之后立刻 flush_ready()，DMA 还在读缓冲区时 LVGL 就已经
     * 把下一帧画进同一块内存 —— 这是滚屏/动画花屏、横向撕裂的直接原因。 */
    esp_err_t err = lcd_draw_bitmap_sync(area->x1, area->y1,
                                        area->x2 + 1, area->y2 + 1,
                                        color_map, LCD_FLUSH_TIMEOUT_MS);
    if (err == ESP_ERR_TIMEOUT) {
        /* 超时一般是总线配置异常；不阻塞 UI，只在前几次打日志 */
        if (s_flush_timeout_cnt < 3) {
            ESP_LOGW(TAG, "刷新等待 DMA 完成超时 (%u)", (unsigned)(s_flush_timeout_cnt + 1));
        }
        s_flush_timeout_cnt++;
    }

    /* 即使超时也必须调用，否则 LVGL 会永久卡在等这块缓冲区 */
    lv_disp_flush_ready(drv);
}

/* 每个刷新周期开始（此时只做 CPU 渲染、还没碰面板）→ 标记"下一块 flush 要等 TE"。
 * TE 未接线时 lcd_wait_te() 会自动降级，这里不必额外判断。 */
static void lvgl_render_start_cb(lv_disp_drv_t *drv)
{
    (void)drv;
    s_flush_needs_te = true;
}

static void lvgl_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    /* 对齐到偶数像素，符合 QSPI AMOLED 传输特性 */
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    if (!s_touch_handle) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint8_t count = 0;

    esp_lcd_touch_read_data(s_touch_handle);
    bool pressed = esp_lcd_touch_get_coordinates(s_touch_handle, x, y, NULL, &count, 1);

    /* 息屏状态下触摸只负责点亮屏幕，不触发点击 */
    if (!s_disp_on) {
        if (pressed) {
            if (s_panel_handle) {
                esp_lcd_panel_disp_on_off(s_panel_handle, true);
                lcd_set_brightness(s_panel_handle, (uint8_t)(s_bright_pct * 255 / 100));
            }
            s_disp_on = true;
            s_last_activity_us = esp_timer_get_time();
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (pressed && count > 0) {
        s_last_activity_us = esp_timer_get_time();
        data->point.x = x[0];
        data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_tick_timer_cb(void *arg)
{
    lv_tick_inc(2);
}

/* ---------------------------------------------------------------
 * LVGL 周期性渲染与定时任务线程
 * --------------------------------------------------------------- */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL 渲染引擎任务已启动 (Core 0)");

    uint32_t timer_cnt = 0;
    while (1) {
        uint32_t delay_ms = 10;
        if (ui_lock(100)) {
            delay_ms = lv_timer_handler();

            /* 每 200ms 刷新当前前台页面的状态数据 */
            timer_cnt += delay_ms;
            if (timer_cnt >= 200) {
                timer_cnt = 0;
                if (s_current_view == UI_VIEW_HOME) {
                    ui_view_home_update();
                } else if (s_current_view == UI_VIEW_PLAYER) {
                    ui_view_player_update();
                } else if (s_current_view == UI_VIEW_UAC) {
                    ui_view_uac_update();
                } else if (s_current_view == UI_VIEW_FILES) {
                    ui_view_files_update();
                } else if (s_current_view == UI_VIEW_SETTINGS) {
                    ui_view_settings_update();
                } else if (s_current_view == UI_VIEW_USB_MODE) {
                    ui_view_usbmode_update();
                }

                /* 自动息屏：超时后关闭显示，触摸点亮 */
                if (s_auto_off_sec > 0 && s_disp_on &&
                    (esp_timer_get_time() - s_last_activity_us) > (int64_t)s_auto_off_sec * 1000000LL) {
                    if (s_panel_handle) {
                        esp_lcd_panel_disp_on_off(s_panel_handle, false);
                    }
                    s_disp_on = false;
                }
            }

            ui_unlock();
        }
        if (delay_ms < 5) delay_ms = 5;
        if (delay_ms > 50) delay_ms = 50;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* 定时重启回调（离开 DAC 页释放 USB 声卡用） */
static void ui_restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

/* ---------------------------------------------------------------
 * UI 切换
 * --------------------------------------------------------------- */
void ui_switch_view(ui_view_type_t view)
{
    /* 只允许在 LVGL 上下文内调用（控件事件回调 / lvgl_task 内）。
     * 那时 LVGL 互斥锁已由调用方持有，这里不能再 ui_lock ——
     * FreeRTOS 普通互斥量不允许同任务重入，重入会直接失败，切页静默失效。 */
    /* 离开 USB DAC 页面：TinyUSB 无法卸载声卡，占用的 USB 只能靠重启释放。
     * 只要声卡已启动且目标不是 UAC 页，一律安排重启（不依赖旧视图状态）。 */
    if (view != UI_VIEW_UAC && usb_uac_service_is_started()) {
        ESP_LOGW(TAG, "离开 DAC 页面：声卡占用 USB 无法卸载，0.8 秒后自动重启");
        static esp_timer_handle_t s_restart_timer = NULL;
        if (!s_restart_timer) {
            const esp_timer_create_args_t targs = { .callback = ui_restart_cb, .name = "ui_restart" };
            if (esp_timer_create(&targs, &s_restart_timer) == ESP_OK) {
                esp_timer_start_once(s_restart_timer, 800000ULL);
            } else {
                ESP_LOGE(TAG, "创建重启定时器失败，立即重启");
                esp_restart();
            }
        }
    }

    s_current_view = view;
    s_last_activity_us = esp_timer_get_time();
    if (view == UI_VIEW_HOME && s_screen_home) {
        lv_scr_load(s_screen_home);
    } else if (view == UI_VIEW_PLAYER && s_screen_player) {
        lv_scr_load(s_screen_player);
    } else if (view == UI_VIEW_UAC && s_screen_uac) {
        lv_scr_load(s_screen_uac);
    } else if (view == UI_VIEW_FILES && s_screen_files) {
        lv_scr_load(s_screen_files);
    } else if (view == UI_VIEW_SETTINGS && s_screen_settings) {
        lv_scr_load(s_screen_settings);
    } else if (view == UI_VIEW_USB_MODE && s_screen_usb_mode) {
        lv_scr_load(s_screen_usb_mode);
    }
}

/* ---------------------------------------------------------------
 * UI 管理器初始化
 * --------------------------------------------------------------- */
esp_err_t ui_manager_init(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io)
{
    ESP_LOGI(TAG, "正在初始化 UI 管理器与 LVGL 8.3...");

    s_panel_handle = panel;
    s_lvgl_mux = xSemaphoreCreateMutex();

    /* 1. 初始化 LVGL 库 */
    lv_init();

    /* 2. 在 PSRAM 中分配双帧缓冲区 (460 * 46 * 2 = 42.3KB 每块)
     *    ESP32-S3 的 SPI DMA 能直接读 PSRAM，放 PSRAM 没问题；
     *    安全性依赖 flush_cb 里「等 DMA 完成再 flush_ready」，不要改回异步提交。 */
    size_t buf_size = LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color_t);
    lv_color_t *buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "PSRAM 分配 LVGL 渲染缓冲区失败！");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUF_LINES);

    /* 3. 注册显示设备驱动 */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = LCD_H_RES;
    s_disp_drv.ver_res = LCD_V_RES;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.rounder_cb = lvgl_rounder_cb;
    s_disp_drv.render_start_cb = lvgl_render_start_cb;   /* 每帧开始前对齐 TE/vblank */
    s_disp_drv.draw_buf = &s_disp_buf;
    s_disp_drv.user_data = panel;
    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);

    /* 4. 注册触控驱动 (CST820) */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_CLK_HZ;
    /* 传 i2c_master_bus_handle_t 才会走 V2（新 I2C 驱动，复用 board_init 建的总线）；
       传端口号强转 esp_lcd_i2c_bus_handle_t 会落到 V1（旧驱动），
       旧驱动的启动冲突检查发现新驱动存在就 abort()，导致开机崩溃重启。 */
    esp_err_t tp_err = ESP_ERR_INVALID_STATE;
    if (!board_i2c_bus_get()) {
        ESP_LOGE(TAG, "I2C 总线未初始化，触摸不可用");
    } else {
        tp_err = esp_lcd_new_panel_io_i2c(board_i2c_bus_get(), &tp_io_cfg, &tp_io);
        if (tp_err != ESP_OK) {
            ESP_LOGE(TAG, "触摸 panel IO 创建失败: %s", esp_err_to_name(tp_err));
        }
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = (gpio_num_t)TOUCH_PIN_RST,
        .int_gpio_num = (gpio_num_t)TOUCH_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    if (tp_io) {
        tp_err = esp_lcd_touch_new_i2c_cst820(tp_io, &tp_cfg, &s_touch_handle);
        if (tp_err != ESP_OK) {
            ESP_LOGE(TAG, "CST820 触摸驱动初始化失败: %s", esp_err_to_name(tp_err));
            s_touch_handle = NULL;
        }
    }
    s_touch_ok = (s_touch_handle != NULL);
    if (!s_touch_ok) {
        ESP_LOGE(TAG, "触摸不可用：请检查 I2C 总线/复位引脚/供电");
    }

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    /* 5. 创建 2ms 高精度心跳定时器 */
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    esp_timer_create(&tick_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 2000);

    /* 6. 创建两套页面视图 */
    if (ui_lock(portMAX_DELAY)) {
        s_screen_home = ui_view_home_create();
        s_screen_player = ui_view_player_create();
        s_screen_uac = ui_view_uac_create();
        s_screen_files = ui_view_files_create();
        s_screen_settings = ui_view_settings_create();
        s_screen_usb_mode = ui_view_usbmode_create();

        /* 默认呈现桌面主页（2x2 宫格） */
        lv_scr_load(s_screen_home);
        s_last_activity_us = esp_timer_get_time();
        ui_unlock();
    }

    /* 7. 启动常驻渲染线程 (Core 0) */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 6144, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "UI 系统初始化完成，全新主界面已点亮！");
    return ESP_OK;
}
