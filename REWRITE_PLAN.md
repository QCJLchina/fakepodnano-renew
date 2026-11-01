# FAKE_POD_NANO 固件重写方案

> 目标：解决两个核心痛点  
> **① 本地播放解码格式太少**（只有 FLAC + MP3）  
> **② USB 声卡采样率被卡死在 16bit/44.1kHz**（CS43131 实际支持 32bit/384kHz/DSD）  
> 新增：UI 重新设计

---

## 一、问题定位（已完成的分析结论）

### 1.1 硬件能力 vs 软件现状

| 层面 | 硬件/固件实际能力 | 现状 | 结论 |
|---|---|---|---|
| **CS43131 DAC** | 32bit / 384kHz PCM，**DSD64/128/256** | UAC 只暴露 16bit/44.1kHz | ❌ 软件限制 |
| **固件内部采样率表** | 8000 ~ 96000 Hz 全阶（`0x3C1AE974`） | 内部其实支持高采样率 | ✅ 有基础 |
| **解码器** | FLAC + MP3 | 仅 2 种 | ❌ 太少 |
| **ESP32-S3 资源** | 16MB Flash + 8MB PSRAM，240MHz 双核 | 只用了 1.9MB / 5.1MB | ✅ 余量充足 |

**关键证据**——固件里内置的采样率表（DROM `0x3C1AE974`）：

```c
96000, 88200, 64000, 48000, 44100, 32000,
24000, 22050, 16000, 12000, 11025,  8000
```

以及 IROM 字面量池（`0x42001C18`, `0x42040210`）中的 `192000`、`384000` 引用。

**→ 固件内部完全有能力处理高采样率，只是 UAC 描述符没声明。**

### 1.2 为什么 UAC 只有 16bit/44.1kHz

TinyUSB 的 UAC 描述符是**编译期固定**的（`tusb_config.h` + 描述符数组）。作者把默认值写死成了 16bit/44.1kHz，Windows 就按这个枚举。

**修复思路**：重写 UAC 描述符，声明完整的采样率与位深组合。

---

## 二、总体架构

```
┌─────────────────────────────────────────────────────────┐
│                      应用层 (C++)                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ 播放器    │ │ UAC 声卡  │ │ 文件浏览  │ │ 设置     │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
├─────────────────────────────────────────────────────────┤
│                    UI 层 (LVGL 9)                        │
│  页面管理 / 主题 / 控件 / 动画 / 输入(陀螺仪+按键+触摸)    │
├─────────────────────────────────────────────────────────┤
│                   服务层                                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │ 解码服务  │ │ 音频管线  │ │ 存储服务  │ │ 电源管理  │   │
│  │ AAC/FLAC │ │ I2S+DMA  │ │ SDMMC    │ │ 电量/休眠 │   │
│  │ WAV/DSD  │ │ 重采样    │ │ FatFs    │ │          │   │
│  │ MP3      │ │ 缓冲      │ │ 播放列表  │ │          │   │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘   │
├─────────────────────────────────────────────────────────┤
│                   驱动层 (ESP-IDF)                       │
│  CO5300(QSPI) │ CST820(I2C) │ CS43131(I2S+I2C)          │
│  QMI8658(I2C) │ SDMMC       │ TinyUSB(UAC+MSC+CDC)      │
└─────────────────────────────────────────────────────────┘
```

---

## 三、解码器选型（核心需求）

| 格式 | 库 | 体积 | 许可证 | 难度 | 优先级 |
|---|---|---|---|---|---|
| **WAV** | 手写 | ~2KB | — | ⭐ | P0（验证链路） |
| **FLAC** | `dr_flac` | ~60KB | 公有领域 | ⭐ | P0 |
| **MP3** | `minimp3` | ~30KB | CC0 | ⭐ | P1（原有） |
| **AAC/M4A** | `faad2` + `mp4v2` | ~200KB | GPL | ⭐⭐⭐ | P0（你要的） |
| **DSD** | 直接 I2S DSD 输出 | ~5KB | — | ⭐⭐ | P0（你要的） |

### 3.1 DSD 实现要点

CS43131 支持 DSD 直通，关键是**I2S 配置成 DSD 模式**：

```c
// CS43131 通过 I2C 配置进 DSD 模式
// I2S 使用：DSD 左/右通道分别走 DATA 线的 PCM 编码方式（DoP）
// 或 CS43131 原生 DSD 输入（需确认板子是否把 DSD 线接到 DATA）
```

⚠️ **风险点**：需要确认 PCB 上 CS43131 的 DSD 引脚是否接到 ESP32。如果只接了 I2S，则只能用 **DoP（DSD over PCM）** 方式，这是标准做法，兼容性好。

### 3.2 AAC 实现要点

M4A = MP4 容器 + AAC 音频流，需要：
- `mp4v2` 或自己解 MP4 box（`ftyp`/`moov`/`mdat`）
- `faad2` 解 AAC raw frame

**简化方案**：只支持 `.m4a`（不处理视频轨），忽略 `moov` 里非音频 track。

---

## 四、UAC 重写方案（解决 16bit/44.1kHz 问题）

### 4.1 目标能力

```
采样率: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
位深  : 16, 24, 32 bit
通道  : 2 (立体声)
```

### 4.2 技术路线

使用 ESP-IDF 官方 **`usb_device_uac`** 组件（`espressif/usb_device_uac`），它基于 TinyUSB，支持自定义采样率。

```c
usb_device_uac_config_t uac_cfg = {
    .callback = ...,
    .mic_callback = ...,
    .speaker_bit_resolution = 24,        // 支持 16/24/32
    .speaker_sample_rate = 48000,        // 默认
    .speaker_max_bytes = ...,
    ...
};
```

⚠️ **限制**：官方组件默认支持固定采样率。要支持**多采样率切换**，需要：
1. 要么修改组件源码，注册多个 AS interface
2. 要么在 UAC 描述符里声明 **Range 类型**（`bSamFreqType = 0`）+ `wMinFreq`/`wMaxFreq`

**推荐做法**：列出多个离散采样率（`bSamFreqType = 8`），这是 Windows 能正确识别的标准方式。

### 4.3 需要修改的关键点

```c
// 描述符中的 Format Type I descriptor
0x0B, 0x24, 0x02, 0x01,   // bLength, CS_INTERFACE, FORMAT_TYPE, FORMAT_TYPE_I
0x02,                     // bNrChannels = 2
0x03,                     // bSubframeSize = 3 bytes (24bit)
0x18,                     // bBitResolution = 24
0x08,                     // bSamFreqType = 8 个离散率
0x44, 0xAC, 0x00,         // 44100
0x80, 0xBB, 0x00,         // 48000
0x88, 0x58, 0x01,         // 88200
0x00, 0x77, 0x01,         // 96000
0x10, 0xB1, 0x02,         // 176400
0x00, 0xEE, 0x02,         // 192000
0x80, 0x62, 0x05,         // 352800
0x00, 0xDC, 0x05,         // 384000
```

---

## 五、UI 重新设计

### 5.1 设计原则

- **单列卡片式**：460×460 方形屏，信息密度低，突出封面和进度
- **陀螺仪手势**：左右切歌、上下调音量、双击播放/暂停
- **触摸辅助**：点击区域比手势更精准，两者并存
- **深色主题**：AMOLED 黑色省电且对比度高

### 5.2 页面结构

```
主页（播放中）
├── 专辑封面（居中，带轻微缩放动画）
├── 曲名 / 艺术家（滚动省略）
├── 进度条（可拖动）
├── 时间 / 码率 / 格式徽章
└── 底栏图标：列表 / 均衡器 / 设置

列表页 ── 按文件夹 / 专辑 / 艺术家 / 播放列表
设置页 ── 蓝牙 / UAC模式 / 屏幕亮度 / 睡眠 / 关于
UAC 页  ── 连接状态 / 采样率显示 / 波形
```

### 5.3 LVGL 9 相对 V8 的改进

| 项目 | V8 | V9 |
|---|---|---|
| 内存占用 | 较高 | 更低 |
| 渲染性能 | 好 | 更好（支持硬件加速） |
| API | 稳定 | 更清晰 |
| 字体 | 需手动转 | 内置更完善 |

---

## 六、工程结构

```
fakepodnano-fw/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── main/
│   ├── main.cpp
│   ├── board/
│   │   ├── board_config.h        ← 引脚定义（待补全）
│   │   └── board_init.cpp
│   ├── drivers/
│   │   ├── display_co5300.cpp
│   │   ├── touch_cst820.cpp
│   │   ├── dac_cs43131.cpp
│   │   ├── imu_qmi8658.cpp
│   │   └── sd_card.cpp
│   ├── audio/
│   │   ├── audio_pipeline.cpp     ← I2S + DMA + 双缓冲
│   │   ├── decoder_base.h
│   │   ├── decoder_wav.cpp
│   │   ├── decoder_flac.cpp
│   │   ├── decoder_mp3.cpp
│   │   ├── decoder_aac.cpp
│   │   └── decoder_dsd.cpp
│   ├── usb/
│   │   ├── usb_descriptors.c      ← UAC 描述符（重点改）
│   │   └── usb_uac.cpp
│   ├── ui/
│   │   ├── ui_manager.cpp
│   │   ├── page_player.cpp
│   │   ├── page_list.cpp
│   │   ├── page_settings.cpp
│   │   └── theme.cpp
│   └── services/
│       ├── playlist.cpp
│       ├── storage.cpp
│       └── power.cpp
└── components/
    ├── dr_flac/
    ├── minimp3/
    ├── faad2/
    └── mp4v2/
```

---

## 七、分区表（16MB 优化）

原固件有 9.9MB SPIFFS 但**完全没用**（音乐走 TF 卡）。建议重新分配：

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x4000,
otadata,  data, ota,     0xd000,   0x2000,
phy_init, data, phy,     0xf000,   0x1000,
app0,     app,  ota_0,   0x10000,  0x600000,   # 6MB（原 3MB）
app1,     app,  ota_1,   0x610000, 0x600000,   # 6MB
coredump, data, coredump,0xC10000, 0x10000,
storage,  spiffs,data,   0xC20000, 0x3E0000,   # 3.8MB 资源
```

**理由**：解码器多了，app 分区从 3MB 扩到 6MB；SPIFFS 从 9.9MB 缩到 3.8MB（够放字体、图片）。

---

## 八、实施路线（分阶段）

### 阶段 0：准备（当前）
- [ ] **提取全部引脚**（JTAG 或试探）
- [ ] 装 ESP-IDF v5.5
- [ ] 建立工程骨架，能编译

### 阶段 1：硬件点亮
- [ ] CO5300 亮屏（官方 example 改引脚）
- [ ] LVGL 跑起来，显示 "Hello"
- [ ] SD 卡挂载，列出文件
- [ ] I2C 扫描确认 0x15 / 0x30 / 0x6B

### 阶段 2：音频基础
- [ ] CS43131 I2C 初始化（查数据手册）
- [ ] I2S 输出，播 WAV（验证链路）
- [ ] 播放 FLAC（dr_flac）
- [ ] 播放 MP3（minimp3）

### 阶段 3：扩展格式
- [ ] AAC/M4A（faad2 + mp4v2）
- [ ] DSD（DoP 方式）

### 阶段 4：USB 功能
- [ ] UAC 描述符重写，支持 16/24/32bit × 全采样率
- [ ] Windows 验证能选到 24bit/192kHz
- [ ] MSC 模式（当 U 盘）

### 阶段 5：UI
- [ ] 播放页
- [ ] 列表页
- [ ] 设置页
- [ ] 陀螺仪手势

### 阶段 6：打磨
- [ ] 电源管理、休眠
- [ ] BLE OTA
- [ ] 性能优化

---

## 九、待确认的关键信息

| 信息 | 状态 | 方案 |
|---|---|---|
| 屏幕 QSPI 引脚 | ❌ | JTAG 读内存 / 试探 |
| 屏幕 RST/DC/背光引脚 | ❌ | 同上 |
| I2S 引脚 | ❌ | 同上 |
| SDMMC 引脚 | ❌ | 同上 |
| 按键 GPIO | ❌ | 同上 |
| **CS43131 初始化序列** | ❌ | 查 [数据手册](https://www.alldatasheet.jp/html-pdf/1445892/CIRRUS/CS43131/21665/84/CS43131.html) |
| **CS43131 DSD 引脚是否接出** | ❓ | 看 PCB |
| **CO5300 初始化序列** | ✅ | 官方组件 [esp_lcd_co5300](https://components.espressif.com/components/espressif/esp_lcd_co5300) |
| **CST820 驱动** | ✅ | 官方 `esp_lcd_touch_cst816s` 兼容 |

---

## 十、风险提示

1. **引脚未知是最大阻塞** —— 必须先解决，否则寸步难行
2. **CS43131 文档获取** —— Cirrus 的 datasheet 需要注册下载
3. **DSD 硬件通路** —— 需确认 PCB 是否把 DSD 信号接到 ESP32
4. **UAC 多采样率** —— 官方组件可能需要打补丁
5. **原作者未开源** —— 无法参考实现，全部自己写

---

## 附：现有资源

- 完整 16MB 备份：`backup/fakepodnano_full_16MB.bin`
- 固件分析工具：`tools/`（esptool、反汇编、NVS 解析）
- 硬件分析报告：`HARDWARE_ANALYSIS.md`
- 启动日志：`backup/bootlog_1.4.6_115200.log`
