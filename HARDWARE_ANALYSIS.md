# FAKE_POD_NANO 硬件配置逆向报告

> 来源：1.4.6 固件（`PROJECT_N7_IDF` / CS43131 版）静态分析 + 运行时串口日志  
> 芯片：ESP32-S3-WROOM-1-N16R8（16MB Flash + 8MB PSRAM）  
> 生成方式：esptool 读取 + xtensa-esp32s3-elf-objdump 反汇编 + NVS 解析

---

## 1. 芯片与存储

| 项目 | 值 | 证据 |
|---|---|---|
| 芯片型号 | **ESP32-S3 (QFN56) rev v0.2** | `esptool chip-id` |
| CPU | Xtensa LX7 双核 240MHz + LP core | 启动日志 `cpu freq: 240000000 Hz` |
| Flash | **16MB Winbond** (`ef 4018`)，QIO 模式 | `flash-id` + `spi_flash: detected chip: winbond` |
| PSRAM | **8MB Octal (AP Memory)**，80MHz | `octal_psram: density 0x03 (64 Mbit)` |
| 模组型号 | **ESP32-S3-WROOM-1-N16R8** | 综合推断 |
| MAC | `14:c1:9f:3a:a1:d0` | `esptool` |

---

## 2. 已确认的硬件外设

| 外设 | 型号 | 接口 | 地址/参数 | 证据 |
|---|---|---|---|---|
| **主控** | ESP32-S3 | — | — | 日志 |
| **显示屏** | **CO5300** (QSPI AMOLED) | QSPI (SPI) | 驱动 `esp_lcd_co5300` v2.0.3 | `co5300_spi: LCD panel create success, version: 2.0.3` |
| **触摸** | **CST820** | I2C | **0x15**，IC id=183 | `CST820: IC id: 183` + I2C 扫描 |
| **DAC** | **CS43131** | I2S + I2C | **I2C 0x30** | I2C 扫描 + `CS43131` 字符串 |
| **陀螺仪** | **QMI8658** | I2C | **0x6B**，6 轴 | `Init QMI8658...` + I2C 扫描 |
| **存储卡** | TF/SD 卡 | **SDMMC** (非 SPI) | 4-bit，SDHC 30GB | `TF Card : SDHC 30436MB` |
| **USB** | Type-C 原生 USB | USB-Serial/JTAG + TinyUSB | CDC + UAC + MSC | `USB mode: USB-Serial/JTAG` |
| **蓝牙** | NimBLE 协议栈 | BLE 5.0 | — | `BLE_50:1` |
| **电池** | ADC 电量检测 | ADC | — | `adc_battery_estimation: Use external ADC handle` |

---

## 3. I2C 总线（已确认）

**I2C Bus 0** 扫描结果直接来自设备运行时日志：

```
I2C_BUS: Bus 0 found device at 0x15   ← CST820 触摸
I2C_BUS: Bus 0 found device at 0x30   ← CS43131 DAC
I2C_BUS: Bus 0 found device at 0x6B   ← QMI8658 陀螺仪
```

**I2C 参数**（从 `0x42017060` 函数反汇编得出）：
- SCL 引脚：**GPIO 21**（`movi.n a9, 21` → `i2c_master_bus_config_t.scl_io_num`）
- 时钟频率：**400000 Hz = 400kHz**（`l32r ... 0x61a80`）
- 驱动版本：新 API `i2c_master_bus_config_t`（IDF 5.x）

> ⚠️ SDA 引脚未能从该函数确定（可能由参数传入），需实机验证或试探。

---

## 4. UART / 调试

| 项目 | 值 | 证据 |
|---|---|---|
| 控制台 UART | TX = **GPIO 43**，RX = **GPIO 44** | `GPIO 44 and 43 are used as console UART I/O pins` |
| 波特率 | 115200 | 日志可读 |
| USB 烧录 | 原生 USB，无需按 BOOT | esptool 一次连接成功 |

> **注意**：项目文档提到的 CH340 **并不存在**，本板走原生 USB。43/44 是 ESP32-S3 的默认 UART0 引脚。

---

## 5. 引脚配置机制（重要发现）

板子上电后，引脚号**不是硬编码**，而是通过 **NVS 配置键**管理：

| 键名 | 含义 |
|---|---|
| `c001` … `c026` | GPIO 引脚分配（共 26 个可配置项） |
| `GODVISION` | 触摸驱动选择（值 1） |
| `phy` | PHY 校准标记 |
| `cal_data` / `cal_mac` / `cal_version` | RF 校准数据 |
| `nimble_bond` | BLE 配对信息 |
| `local_irk_1` | BLE 身份解析密钥 |

**配置结构体运行时地址：** `0x3FCABB1C`（位于 `.bss`，启动时由代码填充）

**相关函数：** `0x42017060` — 板级初始化，负责加载配置并初始化 I2C 总线

> **这是重写固件时最重要的信息**：原固件把引脚做成可配置的，说明作者也没把引脚写死在代码里。重写时可以先从 NVS 读，读不到用默认值。

---

## 6. 软件栈清单

| 组件 | 版本 / 说明 |
|---|---|
| **ESP-IDF** | `v5.5.5-dirty`（作者改过 IDF） |
| **LVGL** | V8（`ESP32S3-IDF_AMOLED_LVGL-V8`） |
| **TinyUSB** | 0.21（`tinyusb/tinyusb_0.21/`） |
| **NimBLE** | IDF 内置蓝牙协议栈 |
| **FatFs** | IDF 内置 |
| **esp_lcd_co5300** | 2.0.3（Espressif 官方组件，**公开可用**） |
| **CST820 驱动** | 1.0.3 |
| **libhelix** | MP3 解码（`mp3` 字符串 x10） |
| **libFLAC** / dr_flac | FLAC 解码 |

### 作者自研模块（源码不可得）

| 模块 | 路径 | 说明 |
|---|---|---|
| OTA 管理 | `/main/mydriver/ota/my_ota.cpp` | C++ 编写 |
| TinyUSB MSC | `/tinyusb/tinyusb_0.21/my_tiny_usb/tusb_msc_storage.c` | U 盘模式 |
| Arduino 兼容层 | `./components/my_arduino_compat/src/my_esp_arduino.cpp` | 自己实现 `pinMode()` 等 |

---

## 7. 分区表（16MB 满配）

| # | Label | Type | SubType | Offset | End | Size |
|---|---|---|---|---|---|---|
| 0 | `nvs` | data | nvs | `0x09000` | `0x0E000` | 20 KB |
| 1 | `otadata` | data | otadata | `0x0E000` | `0x10000` | 8 KB |
| 2 | `app0` | app | ota_0 | `0x10000` | `0x310000` | 3072 KB |
| 3 | `app1` | app | ota_1 | `0x310000` | `0x610000` | 3072 KB |
| 4 | `spiffs` | data | spiffs | `0x610000` | `0xFF0000` | 10112 KB |
| 5 | `coredump` | data | coredump | `0xFF0000` | `0x1000000` | 64 KB |

**特点：**
- 双 OTA 分区，支持 BLE OTA 回滚
- 9.9MB SPIFFS（当前固件实际未使用，音乐走 TF 卡）
- **注意**：固件启动时报 `assets: partition not found: label=assets subtype=0x40`——说明 1.4.6 期望一个 `assets` 分区，但当前分区表没有，**部分功能可能缺失**

---

## 8. 固件镜像结构

```
入口地址 : 0x403759EC
镜像头   : E9 06 02 4F
段数     : 6
SPI 模式 : 2 (DIO)
SPI 频率 : 80MHz
Flash    : 16MB

Seg0: 0x3C100020  DROM   794.6 KB   ← 字符串/常量/应用描述符
Seg1: 0x3FCA5400  DRAM    20.8 KB
Seg2: 0x40374000  IRAM    16.6 KB
Seg3: 0x42000020  IROM  1009.6 KB   ← 主代码
Seg4: 0x40378260  IRAM   116.4 KB
Seg5: 0x50000000  RTC      32 B
```

**应用描述符：**
```
magic      : 0xABCD5432
version    : 8244c3fa-dirty
project    : PROJECT_N7_IDF
build time : Oct 18 2026 12:14:31
idf ver    : v5.5.5-dirty
```

---

## 9. 关键内存布局

```
Heap (启动时):
  3FCB3350  len 0x363C0  (216 KiB)  RAM
  3FCE9710  len 0x05724  ( 21 KiB)  RAM
  3FCF0000  len 0x08000  ( 32 KiB)  DRAM
  600FE000  len 0x01FE8  (  7 KiB)  RTCRAM
  PSRAM  : 6335 KB + 37 KB 加入堆

运行时：
  MEM: Cache align: 64
  MEM: Psram cache size: 5032 KB
  MEM: Used psram: 5152768
```

---

## 10. 重写固件需要补齐的信息

| 信息 | 状态 | 获取方式 |
|---|---|---|
| 屏幕 QSPI 引脚 (CLK/CS/D0-D3) | ❌ 未知 | 逆向寄存器写序列 / 试探 |
| 屏幕 RST / DC / 背光引脚 | ❌ 未知 | 同上 |
| I2S 引脚 (BCLK/LRCK/DIN/MCLK) | ❌ 未知 | 逆向 `i2s_channel_init` 调用 |
| SDMMC 引脚 (CLK/CMD/D0-D3) | ❌ 未知 | 逆向 `sdmmc_host_init` |
| I2C SDA 引脚 | ⚠️ 部分（SCL=21） | 试探 0..48 |
| 按键 GPIO | ❌ 未知 | 逆向 `iot_button_new_gpio_device` |
| CS43131 初始化寄存器序列 | ❌ 未知 | 逆向或查数据手册 |
| CO5300 屏幕初始化序列 | ❌ 未知 | 官方组件默认值可参考 |

### 建议的引脚确认方法

由于原固件从 NVS 读配置且带默认值，**最可靠的方式是在原固件运行时通过 JTAG 读取**：

```bash
# 用 OpenOCD 连上，读取 RAM 中 0x3FCABB1C 附近的结构体
openocd -f board/esp32s3-builtin.cfg
> mdw 0x3FCABB1C 64
```

这样能一次性拿到全部引脚号，比静态逆向快得多。
