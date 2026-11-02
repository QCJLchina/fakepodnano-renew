/**
 * FAKE_POD_NANO 显示屏驱动实现 — CO5300 QSPI AMOLED
 * ============================================
 * 屏幕：鱼鹰光电 AM200Q460460LK (2.0", 460×460)
 *   有效显示区 35.88mm x 35.88mm，点距 0.078mm → 约 326 PPI
 *   刷新率：CO5300(ICNA3311) 数据手册上限 60Hz（1~60Hz，步进 1Hz）
 *
 * 命令没有按另一份出厂顺序重排。没有这块面板的时序证据之前，保持下面这张表现有顺序：
 *     RFE 00 / R11 + 120ms / R35 00 / RFE 00 / RC4 80 / R3A 55
 *     R53 20 / R63 FF / R2A 00 0A 01 D5 / R2B 00 00 01 CB
 *     R29 + 60ms / R51 FF / R58 07 + 10ms
 *
 * 本文件除面板初始化外，还负责【刷新同步】，这是以前缺的两件事：
 *   1) DMA 完成同步 —— 提交一块像素后等它真正传完，再让 LVGL 复用该缓冲区，
 *      否则 LVGL 会在 DMA 还在读的时候把下一帧画进同一块内存（花屏 / 横向撕裂）
 *   2) TE 同步（可选）—— 每个刷新周期开始前等面板的 vblank 上升沿再写 GRAM，
 *      把写显存的时刻放进消隐期，进一步压低撕裂
 */

#include "lcd_co5300.h"
#include "board_config.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_qspi_amoled.h"

static const char *TAG = "LCD";

/* ---------------------------------------------------------------
 * CO5300 初始化序列（AM200Q460460LK，460x460，16bit RGB565）
 * 顺序以本表为准，文件头注释必须与这里保持一致。
 * --------------------------------------------------------------- */
static const qspi_amoled_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},   /* 退出睡眠模式 */
    {0x35, (uint8_t []){0x00}, 1, 0},     /* TE 设置（与原厂一致：R35 00） */
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},     /* SPI 模式控制 */
    {0x3A, (uint8_t []){0x55}, 1, 0},     /* 像素格式 16bit RGB565 */
    {0x53, (uint8_t []){0x20}, 1, 0},     /* CTRL 显示1 */
    {0x63, (uint8_t []){0xFF}, 1, 0},     /* HBM 模式亮度 */
    /* 列地址 10..469（460 列，含 X_GAP=10）；原厂：R2A 00 0A 01 D5
     * ⚠ 旧版本这里误留了 448x368 面板的 {0..447}/{0..367}，已按原厂值修正。
     *   （真正的窗口在每次 draw_bitmap 时由驱动重发，所以旧值不影响出图，
     *     但留着会误导后来人，且初始化后的默认窗口应该是这块屏自己的。） */
    {0x2A, (uint8_t []){0x00, 0x0A, 0x01, 0xD5}, 4, 0},
    /* 行地址 0..459（460 行）；原厂：R2B 00 00 01 CB */
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xCB}, 4, 0},
    {0x29, (uint8_t []){0x00}, 0, 60},    /* 打开显示器 */
    {0x51, (uint8_t []){0xFF}, 1, 0},     /* 正常模式亮度 */
    {0x58, (uint8_t []){0x07}, 1, 10},
};

/* ---------------------------------------------------------------
 * 运行时状态
 * --------------------------------------------------------------- */
/* 面板句柄（供同步绘制 API 使用） */
static esp_lcd_panel_handle_t s_panel = NULL;

/* DMA 传输完成信号量：由 on_color_trans_done 回调在 ISR 里释放 */
static SemaphoreHandle_t s_trans_done_sem = NULL;

/* TE 撕裂同步 */
static SemaphoreHandle_t s_te_sem = NULL;
static bool s_te_sync_on = (LCD_TE_SYNC_ENABLE != 0);
static volatile uint32_t s_te_timeout_cnt = 0;

/* DMA 完成回调：每提交完一块颜色数据，硬件传完后进这里（ISR 上下文）
 *
 * 注意两点（IDF 5.5 源码核实）：
 *  1) SPI 版 esp_lcd_panel_io 会【忽略】本回调的返回值（i80 才会用它触发 yield），
 *     所以这里返回 hp_task_woken 只是语义正确，实际不会有 portYIELD 效果；
 *     被唤醒的任务最迟在下一个 tick 运行（本项目 FREERTOS_HZ=1000，即 ≤1ms）。
 *  2) 本回调必须留在 IRAM：项目开了 CONFIG_SPI_MASTER_ISR_IN_IRAM，
 *     SPI 的 post_cb 可能在 cache 关闭时被调用，里面不能调任何非 IRAM 函数。 */
static bool IRAM_ATTR lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t io,
                                              esp_lcd_panel_io_event_data_t *edata,
                                              void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;

    BaseType_t hp_task_woken = pdFALSE;
    if (s_trans_done_sem) {
        xSemaphoreGiveFromISR(s_trans_done_sem, &hp_task_woken);
    }
    return (hp_task_woken == pdTRUE);
}

/* TE 上升沿中断：面板每帧消隐期给一个脉冲 */
static void IRAM_ATTR lcd_te_isr(void *arg)
{
    (void)arg;
    BaseType_t hp_task_woken = pdFALSE;
    if (s_te_sem) {
        xSemaphoreGiveFromISR(s_te_sem, &hp_task_woken);
    }
    if (hp_task_woken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief 安装 TE 引脚中断（GPIO6，上升沿）
 *
 * 与面板的 0x35 命令配合使用。注意：原厂 init 写的是 R35 00，
 * 若实测 GPIO6 一直没有脉冲，说明该面板的 TE 输出未使能（部分批次需要
 * 0x35 参数置位才输出），此时本模块会自动降级为自由运行，不会卡住 UI。
 */
static esp_err_t lcd_te_sync_init(void)
{
    if (LCD_PIN_TE < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!s_te_sem) {
        s_te_sem = xSemaphoreCreateBinary();
        if (!s_te_sem) {
            ESP_LOGE(TAG, "TE 信号量创建失败");
            return ESP_ERR_NO_MEM;
        }
    }

    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_PIN_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TE(GPIO%d) 配置失败: %s", LCD_PIN_TE, esp_err_to_name(err));
        return err;
    }

    /* 其它驱动（按键等）可能已经装过 ISR 服务，重复安装会返回 INVALID_STATE，忽略 */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "GPIO ISR 服务安装失败: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add((gpio_num_t)LCD_PIN_TE, lcd_te_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TE(GPIO%d) 中断挂载失败: %s", LCD_PIN_TE, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TE 撕裂同步已就绪 (GPIO%d, 上升沿)", LCD_PIN_TE);
    return ESP_OK;
}

esp_err_t lcd_init(esp_lcd_panel_handle_t *ret_panel,
                   esp_lcd_panel_io_handle_t *ret_io)
{
    ESP_LOGI(TAG, "初始化 QSPI 总线 (SCK=%d, D0=%d, D1=%d, D2=%d, D3=%d, %d MHz)",
             LCD_PIN_PCLK, LCD_PIN_DATA0, LCD_PIN_DATA1, LCD_PIN_DATA2, LCD_PIN_DATA3,
             LCD_QSPI_PCLK_HZ / 1000000);

    /* 0. DMA 完成信号量必须早于 panel IO 创建，否则第一块数据就可能丢信号 */
    if (!s_trans_done_sem) {
        s_trans_done_sem = xSemaphoreCreateBinary();
        if (!s_trans_done_sem) {
            ESP_LOGE(TAG, "DMA 完成信号量创建失败");
            return ESP_ERR_NO_MEM;
        }
    }

    /* 1. QSPI 总线 */
    const spi_bus_config_t bus_cfg = QSPI_AMOLED_PANEL_BUS_QSPI_CONFIG(
        LCD_PIN_PCLK,
        LCD_PIN_DATA0,
        LCD_PIN_DATA1,
        LCD_PIN_DATA2,
        LCD_PIN_DATA3,
        LCD_H_RES * LCD_V_RES * (LCD_BITS_PER_PIXEL / 8));

    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI 总线初始化失败");

    /* 2. 面板 IO */
    ESP_LOGI(TAG, "安装面板 IO (CS=%d)", LCD_PIN_CS);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = QSPI_AMOLED_PANEL_IO_QSPI_CONFIG(
        LCD_PIN_CS, lcd_color_trans_done_cb, NULL);
    io_cfg.pclk_hz = LCD_QSPI_PCLK_HZ;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                                 &io_cfg, &io_handle),
                        TAG, "面板 IO 创建失败");

    /* 3. CO5300 面板 */
    ESP_LOGI(TAG, "安装 CO5300 面板驱动");
    qspi_amoled_vendor_config_t vendor_cfg = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_qspi_amoled(io_handle, &panel_cfg, &panel),
                        TAG, "CO5300 面板创建失败");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "面板复位失败");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "面板初始化失败");

    /* 4. 设置显示偏移：面板在控制器显存里从第 10 列开始（X_GAP=10，原厂一致） */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, LCD_X_GAP, LCD_Y_GAP),
                        TAG, "设置 gap 失败");

    /* 5. 开显示 + 亮度 */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "开显示失败");
    ESP_RETURN_ON_ERROR(panel_qspi_amoled_set_brightness(panel, 0xFF), TAG, "设亮度失败");

    s_panel = panel;

    /* 6. TE 撕裂同步（失败不致命：退化为自由运行） */
    if (s_te_sync_on) {
        if (lcd_te_sync_init() != ESP_OK) {
            s_te_sync_on = false;
            ESP_LOGW(TAG, "TE 同步不可用，改为自由运行刷新");
        }
    }

    ESP_LOGI(TAG, "屏幕初始化完成 (%dx%d, 约 %d PPI, 刷新率上限 %dHz)",
             LCD_H_RES, LCD_V_RES, LCD_PPI, LCD_REFRESH_HZ_MAX);

    if (ret_panel) *ret_panel = panel;
    if (ret_io) *ret_io = io_handle;
    return ESP_OK;
}

/* ---------------------------------------------------------------
 * 刷新同步
 * --------------------------------------------------------------- */
esp_err_t lcd_draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end,
                               const void *color_data, uint32_t timeout_ms)
{
    if (!s_panel || !s_trans_done_sem) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 丢掉上一次可能残留的完成信号，避免把"上一块传完"误当成本次完成 */
    while (xSemaphoreTake(s_trans_done_sem, 0) == pdTRUE) {
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, color_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap 提交失败: %s", esp_err_to_name(err));
        return err;
    }

    if (xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "等待 DMA 完成超时 (%u ms)", (unsigned)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t lcd_wait_te(uint32_t timeout_ms)
{
    if (!s_te_sync_on || !s_te_sem) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 清掉渲染期间累积的边沿，只等"下一个" vblank */
    while (xSemaphoreTake(s_te_sem, 0) == pdTRUE) {
    }

    if (xSemaphoreTake(s_te_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        s_te_timeout_cnt = 0;
        return ESP_OK;
    }

    /* 等不到 TE：没接线 / 面板未输出 / 帧率异常。连续超时就自动降级。 */
    if (++s_te_timeout_cnt >= LCD_TE_TIMEOUT_LIMIT) {
        s_te_sync_on = false;
        gpio_intr_disable((gpio_num_t)LCD_PIN_TE);   /* 别让中断继续白跑 */
        ESP_LOGW(TAG, "TE(GPIO%d) 连续 %u 次超时，判定无 TE 信号 → 自动关闭撕裂同步",
                 LCD_PIN_TE, (unsigned)s_te_timeout_cnt);
    }
    return ESP_ERR_TIMEOUT;
}

bool lcd_te_sync_enabled(void)
{
    return s_te_sync_on;
}

esp_err_t lcd_te_sync_set(bool enable)
{
    if (enable && !s_te_sem) {
        esp_err_t err = lcd_te_sync_init();
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
    }
    if (s_te_sem) {
        if (enable) {
            gpio_intr_enable((gpio_num_t)LCD_PIN_TE);
        } else {
            gpio_intr_disable((gpio_num_t)LCD_PIN_TE);
        }
    }
    s_te_sync_on = enable;
    s_te_timeout_cnt = 0;
    return ESP_OK;
}

esp_err_t lcd_set_brightness(esp_lcd_panel_handle_t panel, uint8_t brightness)
{
    return panel_qspi_amoled_set_brightness(panel, brightness);
}

/* ---------------------------------------------------------------
 * 测试图案：便于目视检查屏幕是否正常
 *   上半部分：红色渐变条
 *   下半部分：绿色渐变条
 *   四角：白/黄/青/品红小方块（检查偏移和方向）
 * --------------------------------------------------------------- */
esp_err_t lcd_show_test_pattern(esp_lcd_panel_handle_t panel)
{
    const int W = LCD_H_RES;
    const int H = LCD_V_RES;
    const size_t px_count = W * H;

    /* 用 PSRAM 分配整屏缓冲（460*460*2 = 423KB） */
    uint16_t *buf = (uint16_t *)heap_caps_malloc(px_count * sizeof(uint16_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM 分配失败（需要 %u 字节）",
                 (unsigned)(px_count * sizeof(uint16_t)));
        return ESP_ERR_NO_MEM;
    }

    /* RGB565 工具宏 */
    #define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) & 0xF8) >> 3))

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t c;
            if (y < H / 2) {
                /* 上半：红渐变（左暗右亮） */
                uint8_t r = (uint8_t)(x * 255 / (W - 1));
                c = RGB565(r, 0, 0);
            } else {
                /* 下半：绿渐变（左暗右亮） */
                uint8_t g = (uint8_t)(x * 255 / (W - 1));
                c = RGB565(0, g, 0);
            }
            buf[y * W + x] = c;
        }
    }

    /* 四角标记：60x60 方块，用于验证偏移与方向 */
    const int M = 60;
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < M; x++) {
            /* 左上：白 */
            buf[y * W + x] = RGB565(255, 255, 255);
            /* 右上：黄 */
            buf[y * W + (W - 1 - x)] = RGB565(255, 255, 0);
            /* 左下：青 */
            buf[(H - 1 - y) * W + x] = RGB565(0, 255, 255);
            /* 右下：品红 */
            buf[(H - 1 - y) * W + (W - 1 - x)] = RGB565(255, 0, 255);
        }
    }

    /* 分带提交：整屏一次 draw_bitmap 会被拆成 13 个 SPI 事务（每块 ≤32KB），
     * 而本驱动没有开 flags.psram_dma_direct，PSRAM 缓冲会经内部 DMA bounce buffer 中转，
     * 排队中的事务最多占用 10 x 32KB = 320KB 内部 RAM → 很容易 ESP_ERR_NO_MEM。
     * 按 46 行一条带提交（和 LVGL 的分带刷新一致），内部缓冲峰值只有约 43KB。 */
    const int BAND_LINES = 46;
    esp_err_t err = ESP_OK;
    for (int y = 0; y < H && err == ESP_OK; y += BAND_LINES) {
        int y_end = (y + BAND_LINES > H) ? H : (y + BAND_LINES);
        err = lcd_draw_bitmap_sync(0, y, W, y_end, buf + (size_t)y * W, 500);
    }
    (void)panel;

    heap_caps_free(buf);
    ESP_LOGI(TAG, "测试图案绘制%s", (err == ESP_OK) ? "完成" : "失败");
    return err;

    #undef RGB565
}
