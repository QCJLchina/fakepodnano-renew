/**
 * FAKE_POD_NANO USB 声卡（UAC）页面
 * =================================
 * 视觉基准：prototype/index.html 的 `.uac-page` 区块 + design_ref/uac.png
 * （460x460 截图，2x 采集，本文件里的数字都是「截图像素 / 2」）。
 *
 * 版式（页面内坐标，页面内边距 18/14：屏幕坐标 = 页面坐标 + (18, 14)）：
 *   header   y=0    高 28  左「桌面」药丸 / 中「USB DAC 384k/32bit」绿徽标 / 右 CS43131
 *   大数字   y=115  高 52  48 号字基线落在页面 y=167（屏幕 181，基线 172）
 *   格式行   y=169         基线 198，32-bit / Dual-Channel MasterHIFI Direct
 *   频谱     y=254  高 70  8 根 16x(8..70) 圆角柱，柱底对齐屏幕 338
 *   信息卡   y=392  高 40  HOST / FORMAT 两栏（屏幕 406..446，设计 406.5..445.5）
 *   手势条   挂在 screen 上（页面有 14px 下内边距，挂 page 会比设计稿高 14px）
 *
 * 颜色一律取 ui_theme.h 的令牌；唯一例外是设计稿
 * `radial-gradient(circle at center top, #141824 0%, #000 70%)` 的顶部底色，
 * 令牌里没有这一档，按任务约定只作为本页背景渐变常量 UAC_COL_BG_TOP 声明。
 * LVGL 8 没有径向渐变，用「垂直渐变 + 70% 处收黑」（bg_grad_stop=180）近似。
 *
 * 数据来源（保持原有契约，不新增任何 USB 启动副作用）：
 *   usb_uac_service_is_started()     声卡模式是否已启动
 *   usb_uac_get_current_sample_rate() 真实采样率 -> 大数字 + kHz
 *   usb_uac_is_streaming() / usb_uac_get_rx_bytes() 是否真在推流 -> 频谱跳不跳
 *   audio_pipeline_owner() / audio_pipeline_get_volume() 输出归属与音量 -> 频谱幅度
 * ui_view_uac_update() 仍由 ui_manager 每 200ms 调用一次，内部不建线程、不建额外 timer。
 *
 * 设计稿 header 里没有原版「退出重启」按钮，但 USB 声卡模式必须重启才能释放 USB：
 * 保留功能又不破坏版式 -> 长按左上角「桌面」药丸 = 退出并重启（短按仍是回桌面）。
 */

#include "ui_view_uac.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "usb_uac_service.h"
#include "audio_pipeline.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------
 * 本页常量（全部来自 design_ref/uac.png 量测）
 * ------------------------------------------------------------------ */
#define UAC_CONTENT_W     (UI_SCR_W - UI_PAD_X * 2)   /* 424 */

#define UAC_HEADER_Y      0
#define UAC_HEADER_H      28
#define UAC_PILL_W        72
#define UAC_BADGE_H       24

#define UAC_RATE_Y        115   /* 48 号字 line_height=52，基线 = 顶端 + 43 */
#define UAC_RATE_H        52

#define UAC_TAG_Y         169   /* 基线 = 顶端 + 15 -> 页面 184 */

#define UAC_VIS_Y         254   /* 顶端；+70 = 柱底 324 */
#define UAC_VIS_H         70
#define UAC_VIS_BAR_W     16
#define UAC_VIS_BAR_GAP   10
#define UAC_VIS_BAR_R     8
#define UAC_VIS_COUNT     8
#define UAC_VIS_W         (UAC_VIS_BAR_W * UAC_VIS_COUNT + UAC_VIS_BAR_GAP * (UAC_VIS_COUNT - 1))
#define UAC_BAR_MIN       8
#define UAC_BAR_MAX       70

#define UAC_FOOTER_Y      392
#define UAC_FOOTER_H      40
#define UAC_FOOTER_R      16
#define UAC_FOOTER_PADX   18
#define UAC_FOOTER_VAL_W  160   /* 左栏值宽，够放 Windows PC (ASIO/WASAPI) */
#define UAC_FOOTER_FMT_W  110   /* 右栏值宽，够放 32-bit Float/PCM */

/* 设计稿背景渐变顶部色（本页唯一允许手写的十六进制，仅用于渐变近似） */
#define UAC_COL_BG_TOP    lv_color_hex(0x141824)

/* ------------------------------------------------------------------
 * 状态
 * ------------------------------------------------------------------ */
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_rate = NULL;
static lv_obj_t *s_lbl_unit = NULL;
static lv_obj_t *s_lbl_tag = NULL;
static lv_obj_t *s_bar[UAC_VIS_COUNT];
static int s_bar_h[UAC_VIS_COUNT];

static uint32_t s_last_sr = 0xFFFFFFFFu;   /* 上次显示的采样率，避免重复 set_text */
static uint64_t s_last_rx = 0;             /* 上次累计接收字节，用来判断有无数据流入 */
static int s_active = -1;                  /* -1 未知 / 0 静默 / 1 推流，仅在变化时改样式 */
static uint32_t s_rng = 0x9E3779B9u;       /* 伪随机（LCG），只用于柱高抖动 */

static esp_timer_handle_t s_reboot_timer = NULL;

/* ------------------------------------------------------------------
 * 小工具
 * ------------------------------------------------------------------ */

/** 透明排版容器（本页私有，只做分组，不用任何背景/描边） */
static lv_obj_t* uac_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    return box;
}

/* ------------------------------------------------------------------
 * 事件
 * ------------------------------------------------------------------ */
static void uac_home_click_cb(lv_event_t *e)
{
    (void)e;
    ui_switch_view(UI_VIEW_HOME);
}

static void uac_reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

/* 长按「桌面」= 退出 USB 声卡模式并重启（释放被 TinyUSB 占用的 USB）。
 * LVGL 在长按后不会再补发 CLICKED，所以短按/长按不会互相干扰。 */
static void uac_home_long_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_reboot_timer) {
        esp_timer_start_once(s_reboot_timer, 250000ULL);
    } else {
        esp_restart();
    }
}

/* ------------------------------------------------------------------
 * 页面构建
 * ------------------------------------------------------------------ */
lv_obj_t* ui_view_uac_create(void)
{
    s_screen = ui_screen_create();
    lv_obj_t *page = ui_page_create(s_screen);

    /* 背景：#141824 -> #000 垂直渐变，70% 处收黑（近似设计的径向渐变） */
    lv_obj_set_style_bg_color(page, UAC_COL_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(page, UI_COL_BG, 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(page, 180, 0);

    /* ---------------- 顶部 header ---------------- */
    lv_obj_t *header = uac_box(page, 0, UAC_HEADER_Y, UAC_CONTENT_W, UAC_HEADER_H);

    /* 左：返回药丸（home 图标 + 桌面，强调色） */
    lv_obj_t *pill = ui_pill_create(header, 0, 0, UAC_PILL_W, UAC_HEADER_H,
                                    UI_COL_PILL, UI_COL_PILL);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(pill, 10, 0);
    lv_obj_set_style_pad_right(pill, 10, 0);
    lv_obj_set_style_pad_column(pill, 4, 0);
    ui_icon_create(pill, 0, 0, &ui_icon_home, UI_COL_ACCENT);
    ui_label_create(pill, "桌面", UI_FONT_CJK_S, UI_COL_ACCENT);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pill, uac_home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(pill, uac_home_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 中：绿色 USB DAC 徽标 */
    lv_obj_t *badge = ui_badge_create(header, 0, 0, "USB DAC 384k/32bit",
                                      UI_COL_GREEN, UI_COL_GREEN_SOFT, UI_COL_GREEN_LINE,
                                      UAC_BADGE_H, NULL);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, (UAC_HEADER_H - UAC_BADGE_H) / 2);

    /* 右：CS43131（青色） */
    lv_obj_t *dac = ui_label_create(header, "CS43131", UI_FONT_SM, UI_COL_CYAN);
    lv_obj_align(dac, LV_ALIGN_RIGHT_MID, 0, 0);

    /* ---------------- 采样率大数字 ---------------- */
    lv_obj_t *rate = uac_box(page, 0, UAC_RATE_Y, UAC_CONTENT_W, UAC_RATE_H);
    lv_obj_set_flex_flow(rate, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rate, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rate, 4, 0);

    /* 工程里最大只有 48 号（设计稿 68px）；数字用宽度自适应，最长 "384.0"，
     * 不存在截断风险，所以不设固定宽度（固定宽度会让 kHz 一起偏移）。 */
    s_lbl_rate = ui_label_create(rate, "--", UI_FONT_HUGE, UI_COL_TEXT);
    lv_obj_set_style_text_letter_space(s_lbl_rate, -1, 0);

    /* 单位：中文档位只有 20 号，基线对齐 = 底部对齐后上提 5px
     * （48 号基线距底 9px，20 号距底 4px，5 = 9 - 4） */
    s_lbl_unit = ui_label_create(rate, "kHz", UI_FONT_MD, UI_COL_CYAN);
    lv_obj_set_style_translate_y(s_lbl_unit, -(9 - 4), 0);

    /* ---------------- 格式副标题 ---------------- */
    s_lbl_tag = ui_label_create(page, "32-bit / Dual-Channel MasterHIFI Direct",
                                UI_FONT_CJK_S, UI_COL_TEXT2);
    lv_obj_set_width(s_lbl_tag, UAC_CONTENT_W);
    lv_label_set_long_mode(s_lbl_tag, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_lbl_tag, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_tag, 0, UAC_TAG_Y);

    /* ---------------- 频谱柱状条 ---------------- */
    lv_obj_t *vis = uac_box(page, (UAC_CONTENT_W - UAC_VIS_W) / 2, UAC_VIS_Y,
                            UAC_VIS_W, UAC_VIS_H);
    lv_obj_set_flex_flow(vis, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vis, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < UAC_VIS_COUNT; i++) {
        lv_obj_t *bar = lv_obj_create(vis);
        lv_obj_remove_style_all(bar);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(bar, UAC_VIS_BAR_W, UAC_BAR_MIN);
        lv_obj_set_style_radius(bar, UAC_VIS_BAR_R, 0);
        lv_obj_set_style_bg_color(bar, UI_COL_CYAN_DEEP, 0);  /* 顶部偏蓝 */
        lv_obj_set_style_bg_grad_color(bar, UI_COL_CYAN, 0);  /* 底部青 */
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);           /* 静默时压暗 */
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        s_bar[i] = bar;
        s_bar_h[i] = UAC_BAR_MIN;
    }

    /* ---------------- 底部信息卡 ---------------- */
    lv_obj_t *footer = ui_surface_create(page, 0, UAC_FOOTER_Y, UAC_CONTENT_W,
                                         UAC_FOOTER_H, UAC_FOOTER_R);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(footer, 12, 0);
    lv_obj_set_style_pad_right(footer, 12, 0);

    /* 左栏 HOST */
    lv_obj_t *host_box = uac_box(footer, 0, 0, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(host_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(host_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(host_box, 4, 0);
    ui_label_create(host_box, "HOST:", UI_FONT_TINY, UI_COL_TEXT2);
    lv_obj_t *host_v = ui_label_create(host_box, "Windows PC (ASIO/WASAPI)", UI_FONT_TINY, UI_COL_TEXT);
    lv_obj_set_width(host_v, 185);
    lv_label_set_long_mode(host_v, LV_LABEL_LONG_CLIP);

    /* 右栏 FORMAT */
    lv_obj_t *fmt_box = uac_box(footer, 0, 0, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(fmt_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fmt_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fmt_box, 4, 0);
    ui_label_create(fmt_box, "FORMAT:", UI_FONT_TINY, UI_COL_TEXT2);
    ui_label_create(fmt_box, "32-bit Float/PCM", UI_FONT_TINY, UI_COL_TEXT);

    /* ---------------- 手势提示条（挂 screen 才能贴到屏幕底 6px） ---------------- */
    ui_gesture_hint_create(s_screen);

    /* 重启用的单次定时器（长按药丸时用），只创建一次 */
    if (!s_reboot_timer) {
        const esp_timer_create_args_t targs = { .callback = uac_reboot_cb,
                                                .name = "uac_reboot" };
        if (esp_timer_create(&targs, &s_reboot_timer) != ESP_OK) {
            s_reboot_timer = NULL;
        }
    }

    /* 首次进入同步一次真实状态 */
    ui_view_uac_update();

    return s_screen;
}

/* ------------------------------------------------------------------
 * 周期刷新（ui_manager 每 200ms 调一次）
 * ------------------------------------------------------------------ */

/** 频谱柱高一步：一次只朝目标走 1/3，200ms 周期下视觉上就是平滑跳动。
 *  只改柱子的高度，重绘面积局限在频谱区，不加额外线程/定时器。 */
static void uac_bars_step(bool active, int vol)
{
    for (int i = 0; i < UAC_VIS_COUNT; i++) {
        int target = UAC_BAR_MIN;
        if (active) {
            s_rng = s_rng * 1664525u + 1013904223u;
            int amp = 18 + vol * 52 / 100;        /* 音量越大，跳动幅度越大 */
            if (amp > UAC_BAR_MAX - UAC_BAR_MIN) amp = UAC_BAR_MAX - UAC_BAR_MIN;
            if (amp < 1) amp = 1;
            target = UAC_BAR_MIN + (int)((s_rng >> 16) % (uint32_t)amp);
        }

        int cur = s_bar_h[i];
        int diff = target - cur;
        cur += (diff >= 0) ? (diff + 2) / 3 : -(((-diff) + 2) / 3);
        if (cur < UAC_BAR_MIN) cur = UAC_BAR_MIN;
        if (cur > UAC_BAR_MAX) cur = UAC_BAR_MAX;

        if (cur != s_bar_h[i]) {
            s_bar_h[i] = cur;
            lv_obj_set_height(s_bar[i], cur);
        }
    }
}

void ui_view_uac_update(void)
{
    if (!s_screen || !s_lbl_rate) return;

    /* ---- 采样率：大数字（kHz 单位单独一个青色标签） ---- */
    uint32_t sr = usb_uac_get_current_sample_rate();
    if (sr != s_last_sr) {
        s_last_sr = sr;
        char buf[16];
        if (sr >= 1000) {
            snprintf(buf, sizeof(buf), "%.1f", (double)sr / 1000.0);   /* 384000 -> 384.0 */
        } else {
            snprintf(buf, sizeof(buf), "--");                          /* 未连接 */
        }
        lv_label_set_text(s_lbl_rate, buf);
    }

    /* ---- 是否真的在推流 ---- */
    bool started = usb_uac_service_is_started();
    bool streaming = usb_uac_is_streaming();
    uint64_t rx = usb_uac_get_rx_bytes();
    bool inflow = (rx > s_last_rx);
    s_last_rx = rx;

    bool active = started && (streaming || inflow ||
                              audio_pipeline_owner() == AUDIO_OWNER_UAC);
    int vol = (int)audio_pipeline_get_volume();

    /* 状态切换时才改不透明度，避免每帧刷样式 */
    int act = active ? 1 : 0;
    if (act != s_active) {
        s_active = act;
        lv_opa_t opa = active ? LV_OPA_COVER : LV_OPA_40;
        for (int i = 0; i < UAC_VIS_COUNT; i++) {
            lv_obj_set_style_bg_opa(s_bar[i], opa, 0);
        }
        lv_obj_set_style_text_color(s_lbl_unit,
                                    active ? UI_COL_CYAN : UI_COL_TEXT2, 0);
    }

    uac_bars_step(active, vol);
}
