# FAKE_POD_NANO 固件实现状态与使用说明

> 本文件记录本轮全部改动、使用方式与已知限制。**所有功能均未运行自动化测试，是否可用请人工验证。**

---

## 一、构建与烧录

- 源码工作区（本机）：`E:\test astra\fakepodnano`（ASCII 路径，可直接编译）
- 环境（本机实测可用）：ESP-IDF v5.5.5（`E:\espressif\esp-idf`）、工具链 `C:\Users\MAXSUN\.espressif`（`idf5.5_py3.14_env`）
- 编译命令：先 `. E:\espressif\esp-idf\export.ps1`，再 `idf.py -B build build`
- 必须设置 `IDF_CCACHE_ENABLE=0`（中文用户名下 ccache 会失败）、`IDF_SKIP_CHECK_SUBMODULES=1`（micro-ecc 子模块拉不下来）
- ⚠️ **两个 LVGL 选项必须保持开启**：`CONFIG_LV_FONT_FMT_TXT_LARGE=y`、`CONFIG_LV_USE_FONT_COMPRESSED=y`。
  16px/20px 中文字库是 RLE 压缩位图（`font_dsc.bitmap_format = 1`）、位图索引超过 1MB；
  缺 LARGE 编译直接 `#error`，缺 FONT_COMPRESSED 则**编译通过但屏幕上一个中文都不显示**（LVGL 打印
  "Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled" 后返回 NULL）。
- 烧录（USB-Serial/JTAG，端口号以设备管理器为准；本机当前是 COM4）：
  ```
  python -m esptool --chip esp32s3 -p COM4 -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin \
    0xd000 ota_data_initial.bin 0x10000 fakepodnano.bin
  ```
- 产物同步到 `firmware/build/`

---

## 二、功能清单（本轮新增/改造）

| 模块 | 状态 | 关键实现位置 |
|---|---|---|
| 主页 2x2 宫格（按设计稿重写） | ✅ | `ui/ui_theme.cpp`、`ui/ui_view_home.cpp`（205x183渐变大卡片+44x44渐变图标底板+状态栏） |
| 音乐目录页（按设计稿重写） | ✅ | `ui/ui_view_files.cpp`（导航头+面包屑+34x34圆角图标位+圆角卡片行） |
| 播放器页面（按设计稿重写） | ✅ | `ui/ui_view_player.cpp`（桌面药丸+格式徽标+黑胶唱盘/封面+进度条+58/72/58大三键+音量HUD） |
| 系统设置页（按设计稿重写） | ✅ | `ui/ui_view_settings.cpp`（7张高质感卡片+6档亮度/EQ/自动息屏/IMU+独立关于本设备页） |
| USB 模式选择页（按设计稿重写）| ✅ | `ui/ui_view_usbmode.cpp`（两张大卡片：USB DAC / U盘模式） |
| USB DAC 仪表盘（按设计稿重写）| ✅ | `ui/ui_view_uac.cpp`（384.0kHz 大字+格式卡+跳动频谱柱+HOST/FORMAT底卡） |
| UI 设计令牌与通用控件 | ✅ | `ui/ui_theme.h`、`ui/ui_theme.cpp`（设计真源原型颜色/尺寸/排版，禁手写魔数） |
| UI 原生 Material 图标资产 | ✅ | `ui/ui_icons.c`、`ui/ui_icons.h`（由 prototype SVG 导出的 RGB565+Alpha 精准位图） |
| 宿主端 LVGL 8.4 渲染器 | ✅ | `sim/`（MSVC+Ninja+全量 CJK 字体，可即时生成 460x460 各视图真实渲染） |
| 全量 CJK 高质量中文字库 | ✅ | `ui/fonts/ui_font_cjk_16.c`、`ui_font_cjk_20.c`（DengXian 16 常规+20 粗体，解决缺字方块） |
| 音频原生直通（44.1/48/88.2/96k × 16/24/32bit，不再统一压成 44.1k/16） | ✅ | `audio/audio_pipeline.cpp`（`hw_rate_supported` / `hw_bits_supported`） |
| 上一曲/下一曲（目录内循环） | ✅ | `services/player_service.cpp`（`player_next/prev`） |
| 播放器三键 | ✅ | `ui/ui_view_player.cpp` |
| 物理按键（GPIO48=功能键，GPIO0=电源键） | ✅ | `services/button_service.cpp` |
| 长按电源键关机（深睡，EXT1 唤醒） | ✅ | `services/button_service.cpp` |
| IMU 手势（QMI8658）+ 设置开关 | ✅ | `services/imu_service.cpp`（WHO_AM_I=0x05 已实测存在） |
| 专辑封面（FLAC PICTURE / MP3 ID3 APIC + tjpgd 解码） | ✅ | `services/album_art.cpp`、`third_party/tjpgd/` |
| EQ 均衡器（3 段，5 预设） | ✅ | `audio/audio_pipeline.cpp` |
| M4A 容器解析（MP4 demux + Helix raw AAC） | ✅ | `audio/decoder_aac.cpp` |
| DSD(DoP) 实验性输出 | ⚠️ 已实现，硬件通路未知 | `audio/decoder_dsd.cpp`（见限制） |
| Apple CTS 标准对时 (0x1805/0x2A2B) | ✅ | `services/ble_time_sync.cpp` |
| BLE OTA (0xFFF0/1/2/3) | ✅ | `services/ble_time_sync.cpp` |
| MSC U盘模式 | ✅ 独立固件变体（已编译、启动路径已验证） | `components/usb_msc_dev`（Kconfig `FAKEPOD_USB_MSC_MODE`） |
| 桌面「USB 模式」选择页（DAC / U盘） | ✅ | `ui/ui_view_usbmode.cpp` |
| 中/日/韩文界面字体 | ✅ | `ui/fonts/ui_font_cjk_16.c`、`ui_font_cjk_20.c`（20px 全 GB2312） |
| UTF-8 文件名 | ✅ | `sdkconfig`: `CONFIG_FATFS_API_ENCODING_UTF_8=y` |
| exFAT 支持 | ✅ | 本地 `components/fatfs` 覆盖，`FF_FS_EXFAT=1` |

---

## 三、使用说明

### 物理按键
- **功能键 GPIO48**：单击=播放/暂停；双击=下一曲；长按(>1.5s)=打开 USB DAC 页（启动声卡）
- **电源键 GPIO0**：短按=息屏/亮屏；长按(>2.5s)=关机（deep sleep，再按电源键开机）

### 触摸手势（播放页）
- 上下滑 = 音量 ±5%（右上角弹「音量 xx%」）
- 左右滑 = 上一曲/下一曲
- 播放页三个按钮：‹ / 播放暂停 / ›；左下 `−` 右下 `+` 调音量

### IMU 手势（设置右上角可开关）
- 左右倾 = 切歌，上下倾 = 音量，双击(敲击) = 播放/暂停；仅播放页生效
- **默认关闭**（开关存 RAM，重启恢复默认关；需要时在设置里打开）
- 识别条件很严：必须接近水平（|gz| ≥ 0.70g）且一次倾斜只触发一次，回中位后才能再触发

### 系统设置
- 屏幕亮度 6 档（真实写 CO5300 亮度寄存器）
- 自动息屏（30秒/1分钟/3分钟/从不），息屏后触摸点亮
- 均衡器：关闭/流行/摇滚/古典/人声
- 关于本设备

### USB 声卡
- 进入「USB DAC」卡片/长按功能键才启动（开机不占用 USB）
- UAC2.0：44.1/48/88.2/96kHz、24bit-in-32、立体声（全速 USB 带宽上限 96k）
- 电脑下发的采样率/位深**直通**到 I2S+CS43131（不再统一压成 44.1k/16bit）：Windows 选「24 位 / 96000 Hz」时 DAC 实际就是 96kHz/24bit

### 本地播放（TF 卡）
- 44.1 / 48 / 88.2 / 96 kHz × 16 / 24 / 32 bit **原生直通**：I2S 槽宽随源格式切换，CS43131 的 ASP 分频档同步重配
- 单声道、22.05k/32k 等非标准帧率、>96kHz 的 PCM 才走软件重采样（≤96k 源落到 44.1k/16bit，>96k 源落到 96k/16bit）
- DoP（DSD over PCM，176.4k/24bit 立体声）原样直通，不重采样

### BLE
- 设备名 `FAKE_POD_NANO`
- 自定义对时 0xFFE0/0xFFE1（网页写 8 字节时间包）
- 标准 CTS 0x1805 / Current Time 0x2A2B（读/写/通知）
- **BLE OTA**：0xFFF0 服务
  - `0xFFF1` 控制：`0x01`+4字节总长=开始，`0x02`=结束并重启，`0x03`=中止
  - `0xFFF2` 数据：WriteWithoutResponse 分块写入
  - `0xFFF3` 状态：1 字节状态(0空闲/2写入/3完成/4错误) + 4 字节已写字节数

---

## 四、已知限制（如实说明）

1. **DSD(DoP)**：已按 DoP 标准封装 24bit/176.4k 输出，但 **CS43131 是否支持 DoP / PCB 是否把 DSD 引脚接出未知**。可能无声或噪声，属硬件通路限制，不影响 PCM 各格式。
2. **MSC U盘模式（已修复底层并支持双分区联动）**：
   - **底层死穴修复**：已在 `components/usb_msc_dev/usb_msc_dev.c` 补齐 `usb_new_phy()`（OTG 内部 PHY 初始化）与 `usb_msc_task`（`tud_task` 轮询任务），并补全 `usb` 驱动依赖与 `audio_pipeline_dac_probe` 导出，解决了原版无法枚举 USB 设备与编译报错问题。
   - **双固件联动机制**：
     - `app0`（0x10000）：主系统固件（UI、播放器、UAC2.0 高保真声卡、蓝牙）；
     - `app1`（0x610000）：U盘专属固件（由 `build_msc` 编译生成）；
     - 在主系统 UI 点击「U 盘 (MSC) 模式」后，自动通过 `esp_ota_set_boot_partition(app1)` 切换并重启进入 U 盘读卡器模式；U 盘固件启动后自动设置下次开机回到 `app0`，断电或重启即自动回到播放器主系统。
   - **单固件独立烧录**：若仅需把设备作为纯 U 盘读卡器，也可直接将 `build_msc/fakepodnano.bin` 单独刷入 `0x10000`。
3. **FAT 文件名**：已切 UTF-8 编码，但 FatFs 代码页仍为 437；极个别符号可能显示异常。
4. **测试**：全部功能未运行自动化测试，是否可用以人工验证为准。
5. 开机若插着 USB 调试，偶发 I2C 扫描毛刺（非功能性问题）。

---

## 五、已知待人工验证点

- MSC U盘：需插入 TF 卡后接电脑确认出现可移动磁盘（代码已编译并进入 MSC 路径）
- DSD(DoP)：需实测是否有声（依赖 DAC 是否支持 DoP / DSD 引脚）
- M4A：需实测可播放
- 陀螺仪「双击」灵敏度需按手感微调（`imu_service.cpp` 中的 `TAP_G` / `TILT_G`）
