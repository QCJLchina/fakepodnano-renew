/**
 * FAKE_POD_NANO 板级定义 - HIFI 版 (CS43131)
 * ============================================
 * 来源：OSHWHub 官方工程 ESP32S3-IDF_AMOLED_LVGL-V8.zip
 *       main/main.c 第 24-99 行注释（作者本人整理的两版 IO 对照表）
 * 芯片：ESP32-S3-WROOM-1-N16R8 (16MB Flash + 8MB PSRAM)
 *
 * 注：本板为 HIFI 版，音频 DAC 为 CS43131
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* ==================================================================
 *  显示屏: 鱼鹰光电 AM200Q460460LK  (2.0" AMOLED, CO5300 驱动)
 * ================================================================== */
#define LCD_H_RES               460
#define LCD_V_RES               460
#define LCD_X_GAP               10      /* 水平偏移，必须设置！ */
#define LCD_Y_GAP               0

#define LCD_PIN_RST             7       /* TFT_RST */
#define LCD_PIN_CS              8       /* TFT_CS  */
#define LCD_PIN_PCLK            9       /* TFT_SCK */
#define LCD_PIN_DATA0           10      /* TFT_SDA0 (QSPI D0) */
#define LCD_PIN_DATA1           11      /* TFT_SDA1 (QSPI D1) */
#define LCD_PIN_DATA2           12      /* TFT_SDA2 (QSPI D2) */
#define LCD_PIN_DATA3           13      /* TFT_SDA3 (QSPI D3) */

#define LCD_PIN_TE              6       /* 撕裂效果同步信号 (可选) */
#define LCD_QSPI_PCLK_HZ        (40 * 1000 * 1000)  /* 40MHz；80MHz 也可但需 PCB 走线良好 */

#define LCD_SPI_HOST            SPI2_HOST
#define LCD_BITS_PER_PIXEL      16      /* RGB565, 0x3A = 0x55 */

/* ------------------------------------------------------------------
 * 显示屏物理 / 电气规格（核实来源：鱼鹰光电官方规格页 AM200Q460460LK
 *   + CO5300 数据手册，CO5300 即集创北方 ICNA3311）
 * ------------------------------------------------------------------ */
#define LCD_PANEL_MODEL         "AM200Q460460LK"
#define LCD_SIZE_INCH           2.0f    /* 对角线 2.0" = 50.8mm */
#define LCD_ACTIVE_AREA_MM      35.88f  /* 有效显示区 35.88mm x 35.88mm（官方值） */
#define LCD_DOT_PITCH_MM        0.078f  /* = 35.88 / 460
                                         * ⚠ 厂商规格页写的 "点距 0.1335mm" 是错的：
                                         *   0.1335 x 460 = 61.4mm，与 35.88mm 矛盾，
                                         *   用它反推会得到 190 PPI 的错误结论 */
#define LCD_PPI                 326     /* 按 2.0": sqrt(460²+460²)/2.0 ≈ 325.3
                                         * 按官方有效区: 460 / 35.88mm x 25.4 ≈ 325.6 */
#define LCD_MM_PER_PX           0.078f  /* 1px ≈ 0.078mm；1mm ≈ 12.82px */

/* 刷新率：CO5300(ICNA3311) 手册标注 1~60Hz（步进 1Hz）→ 60Hz 是硬件上限。
 * 注意：这是芯片手册值，不是实测值；本工程此前任何文档都没有写过刷新率。 */
#define LCD_REFRESH_HZ_MAX      60
#define LCD_REFRESH_HZ_TARGET   60
#define LCD_LVGL_DPI            326     /* 仅作文档参考；实际生效的是 sdkconfig 的
                                         * CONFIG_LV_DPI_DEF（原默认 130） */

/* QSPI 时钟 vs 满屏帧率（460x460 RGB565 = 423200 字节/帧 = 3.386 Mbit）：
 *   40MHz x 4lane = 160Mbit/s → 满屏 21.2ms → 约 47fps 上限
 *   80MHz x 4lane = 320Mbit/s → 满屏 10.6ms → 约 94fps 上限
 * 结论：40MHz 跑不满 60Hz 的整屏刷新，但 LVGL 局部刷新（脏矩形）完全够用；
 *       若 PCB 走线允许（原厂注明 80MHz 可行、杜邦线不行），可把 LCD_QSPI_PCLK_HZ 改 80MHz。 */

/* ------------------------------------------------------------------
 * 撕裂（tearing）同步 —— TE 信号接 GPIO6
 * 开启后，每个刷新周期的【第一块】数据写 GRAM 之前先等一个 TE 上升沿。
 * 若 TE 未接线 / 面板没输出 TE（0x35 参数为 00），连续超时后会自动降级为
 * 自由运行并打警告，不会卡死 UI。置 0 可彻底关闭。
 * ------------------------------------------------------------------ */
#define LCD_TE_SYNC_ENABLE      1
#define LCD_TE_TIMEOUT_MS       20      /* >1 帧(16.7ms)，等不到就放弃本次同步 */
#define LCD_TE_TIMEOUT_LIMIT    12      /* 连续超时 N 次 → 自动降级 */

/* 等待一次 DMA 传输完成的超时（正常一块 46 行 ≈ 2.1ms @40MHz） */
#define LCD_FLUSH_TIMEOUT_MS    100

/* CO5300 初始化序列（作者验证过的完整版本） */
#define LCD_INIT_CMD_COUNT      13

/* ==================================================================
 *  触摸: CST820 (I2C 地址 0x15)
 * ================================================================== */
#define TOUCH_PIN_SDA           3       /* I2C_WIRE_SDA */
#define TOUCH_PIN_SCL           2       /* I2C_WIRE_SCL */
#define TOUCH_PIN_RST           5       /* TOUCH_PIN_NUM_RST */
#define TOUCH_PIN_INT           4       /* TOUCH_PIN_NUM_INT */

#define TOUCH_I2C_HOST          I2C_NUM_0
#define TOUCH_I2C_ADDR          0x15
#define TOUCH_I2C_CLK_HZ        (200 * 1000)   /* 官方示例用 200kHz */

/* ==================================================================
 *  音频: CS43131 (I2C 地址 0x30) + I2S
 * ================================================================== */
#define AUDIO_I2S_MCK_IO        (-1)    /* MCK 未接 */
#define AUDIO_I2S_BCK_IO        40      /* BCK  位时钟 */
#define AUDIO_I2S_WS_IO         38      /* LCK/WS 左右声道时钟 */
#define AUDIO_I2S_DO_IO         39      /* DIN  数据输出 */
#define AUDIO_DAC_RST_PIN       41      /* CS43131_RST（低电平静音）*/

#define AUDIO_DAC_I2C_ADDR      0x30

/* 普通版区别（本校为 HIFI 版，仅记录）：
 *   普通版 BTN_PIN = 47, 无 IMU_INT1, 用 AUDIO_MUTE_PIN = 48 静音
 */

/* ==================================================================
 *  存储: TF 卡 SDMMC 4-bit
 * ================================================================== */
#define SD_PIN_D0               15
#define SD_PIN_D1               14
#define SD_PIN_D2               21
#define SD_PIN_D3               18
#define SD_PIN_CLK              16
#define SD_PIN_CMD              17

/* 注：需要 GPIO 21/14/15/18 内部上拉，TF 卡才能稳定识别 */

/* ==================================================================
 *  IMU: QMI8658 (I2C 地址 0x6B, 与触摸共用 I2C 总线)
 * ================================================================== */
#define IMU_I2C_ADDR            0x6B
#define IMU_INT1_PIN            47      /* IMU_INT1 */

/* ==================================================================
 *  其他
 * ================================================================== */
#define BTN_PIN                 48      /* 用户按键 */
#define BAT_ADC_PIN             1       /* 电池电压检测 ADC */

/* 麦克风 PDM */
#define MIC_PDM_DATA            46
#define MIC_PDM_SCK             45
#define MIC_I2S_PORT            I2S_NUM_0

/* ==================================================================
 *  USB
 * ================================================================== */
/* 原生 USB (GPIO 19/20，芯片内置，无需配置)
 * 支持: CDC (串口) / UAC (声卡) / MSC (U盘)
 * 注意：同一时间只能工作在其中一种模式
 */

/* ==================================================================
 *  完整引脚占用速查表
 * ==================================================================
 *  GPIO | 用途                    | 备注
 *  -----+-------------------------+---------------------------
 *   1   | BAT_ADC                 | 电池电压
 *   2   | I2C_SCL                 | 触摸+IMU 共用
 *   3   | I2C_SDA                 | 触摸+IMU 共用
 *   4   | TOUCH_INT               | 触摸中断
 *   5   | TOUCH_RST               | 触摸复位
 *   6   | LCD_TE                  | 屏幕撕裂同步
 *   7   | LCD_RST                 | 屏幕复位
 *   8   | LCD_CS                  | QSPI 片选
 *   9   | LCD_PCLK                | QSPI 时钟
 *   10  | LCD_DATA0               | QSPI D0
 *   11  | LCD_DATA1               | QSPI D1
 *   12  | LCD_DATA2               | QSPI D2
 *   13  | LCD_DATA3               | QSPI D3
 *   14  | SD_D1                   | SD 卡数据1
 *   15  | SD_D0                   | SD 卡数据0
 *   16  | SD_CLK                  | SD 卡时钟
 *   17  | SD_CMD                  | SD 卡命令
 *   18  | SD_D3                   | SD 卡数据3
 *   19  | USB_D-                  | 原生 USB (芯片内置)
 *   20  | USB_D+                  | 原生 USB (芯片内置)
 *   21  | SD_D2                   | SD 卡数据2
 *   38  | I2S_WS (LRCK)           | 音频左右声道
 *   39  | I2S_DOUT                | 音频数据输出
 *   40  | I2S_BCLK                | 音频位时钟
 *   41  | CS43131_RST             | DAC 复位
 *   43  | UART0_TX                | 调试串口
 *   44  | UART0_RX                | 调试串口
 *   45  | MIC_PDM_SCK             | 麦克风时钟
 *   46  | MIC_PDM_DATA            | 麦克风数据
 *   47  | IMU_INT1                | 陀螺仪中断
 *   48  | BTN                     | 用户按键
 */

#endif /* BOARD_CONFIG_H */
