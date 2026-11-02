/**
 * FAKE_POD_NANO USB 模式选择页实现
 * ==================================
 * 视觉基准：主页卡片语言（`ui_card_create` 大圆角渐变卡 + 图标底板 + 标题/说明）
 *           → design_ref/launcher.png 卡片风格。
 *
 * 版式（页面内边距 18/14，子对象坐标从页面内容区起算，内容宽 424）：
 *   导航头   屏幕 y=14：左「桌面」药丸 + 标题「USB 模式」
 *   大卡片   屏幕 y=58 起，424x110，圆角 24，图标底板 44x44 + 标题 + 说明
 *   提示     次级灰小字
 *   手势条   底部居中
 *
 * 行为保持与旧实现一致：
 *   - USB DAC 声卡：usb_uac_service_start() 成功后切到 UAC 页面
 *   - U盘(MSC) 模式：MSC 为独立固件变体，本固件只提示，不触发切换/重启
 */

#include "ui_view_usbmode.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "usb_uac_service.h"
#include "esp_system.h"
#include "esp_log.h"

#if defined(ESP_PLATFORM)
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define CARD_W   424
#define CARD_H   110
#define CARD_GAP 14
#define CARD_Y0  44              /* 页面内容区 y：屏幕 58 */

static const char *TAG = "USB_MODE";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_msg = NULL;

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void dac_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "=== 用户点击了 USB DAC 声卡卡片 ===");
    if (usb_uac_service_start() == ESP_OK) {
        ui_switch_view(UI_VIEW_UAC);
    }
}

static void msc_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "=== 用户点击了 U 盘 (MSC) 模式卡片 ===");
#if defined(ESP_PLATFORM)
    const esp_partition_t *msc_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (msc_part) {
        ESP_LOGI(TAG, "找到 MSC 固件分区: %s (偏移: 0x%lx, 大小: %lu 字节)",
                 msc_part->label, (unsigned long)msc_part->address, (unsigned long)msc_part->size);
        esp_err_t err = esp_ota_set_boot_partition(msc_part);
        ESP_LOGI(TAG, "esp_ota_set_boot_partition 结果: %s", esp_err_to_name(err));
        if (err == ESP_OK) {
            if (s_msg) {
                lv_label_set_text(s_msg, "正在重启进入 U 盘读卡器模式...");
                lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
            }
            lv_refr_now(NULL);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            return;
        } else {
            if (s_msg) {
                char err_buf[64];
                snprintf(err_buf, sizeof(err_buf), "OTA引导设置失败: %s", esp_err_to_name(err));
                lv_label_set_text(s_msg, err_buf);
                lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
            }
            lv_refr_now(NULL);
            return;
        }
    } else {
        ESP_LOGE(TAG, "未找到 app1 (OTA_1) 分区！");
    }
#endif
    if (!s_msg) return;
    lv_label_set_text(s_msg, "未在 app1 分区找到 MSC 固件。请将 build_msc/fakepodnano.bin 烧录至 0x610000。");
    lv_obj_clear_flag(s_msg, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);
}

/** 一张大卡片：图标底板 + 标题 + 说明 + 右侧箭头 */
static lv_obj_t* mode_card(lv_obj_t *parent, int y,
                           const lv_img_dsc_t *icon, lv_color_t g1, lv_color_t g2,
                           const char *title, const char *desc,
                           lv_event_cb_t cb)
{
    lv_obj_t *card = ui_card_create(parent, 0, y, CARD_W, CARD_H);

    lv_obj_t *tile = ui_icon_tile_create(card, 18, 18, 44, 14, icon, g1, g2);

    lv_obj_t *t = ui_label_create(card, title, UI_FONT_CJK_M, UI_COL_TEXT);
    lv_obj_set_width(t, 260);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t, 76, 20);

    lv_obj_t *d = ui_label_create(card, desc, UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(d, 300);
    lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(d, 76, 56);

    lv_obj_t *arrow = ui_label_create(card, LV_SYMBOL_RIGHT, UI_FONT_SM, UI_COL_ACCENT);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -18, 0);

    /* 保证卡片本身及所有文字/图标子元素均能响应点击 */
    lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(d, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(arrow, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arrow, LV_OBJ_FLAG_CLICKABLE);

    return card;
}

lv_obj_t* ui_view_usbmode_create(void)
{
    s_screen = ui_screen_create();
    lv_obj_t *page = ui_page_create(s_screen);

    ui_nav_header_create(page, 14, "桌面", "USB 模式", back_home_cb, NULL);

    mode_card(page, CARD_Y0, &ui_icon_headset, UI_GRAD_UAC_A, UI_GRAD_UAC_B,
              "USB DAC 声卡", "把本机当作电脑 USB 声卡使用", dac_cb);

    mode_card(page, CARD_Y0 + CARD_H + CARD_GAP, &ui_icon_folder,
              UI_GRAD_FILES_A, UI_GRAD_FILES_B,
              "U盘 (MSC) 模式", "把 TF 卡当作电脑 U 盘使用", msc_cb);

    lv_obj_t *hint = ui_label_create(page, "提示：USB 声卡会占用 USB 接口；U 盘模式需重启切换到 MSC 固件。",
                                     UI_FONT_CJK_S, UI_COL_TEXT3);
    lv_obj_set_width(hint, CARD_W);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(hint, 0, CARD_Y0 + 2 * (CARD_H + CARD_GAP) + 8);

    s_msg = ui_label_create(page, "", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_set_width(s_msg, CARD_W);
    lv_label_set_long_mode(s_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_msg, 0, CARD_Y0 + 2 * (CARD_H + CARD_GAP) + 52);
    lv_obj_add_flag(s_msg, LV_OBJ_FLAG_HIDDEN);

    ui_gesture_hint_create(s_screen);
    return s_screen;
}

void ui_view_usbmode_update(void)
{
}
