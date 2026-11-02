# FAKE_POD_NANO 固件实现状态与使用说明

> 本文件记录本轮全部改动、使用方式与已知限制。**所有功能均未运行自动化测试，是否可用请人工验证。**

---

## 一、构建与烧录

- 源码工作区：`D:\task\fakepodnano固件重写`（中文路径不能直接编译，见下）
- **必须在 ASCII 路径编译**：把 `firmware/` 复制到 `D:\Espressif\build-src`，用 `idf.py -B D:\Espressif\fw-build build`
- 环境：ESP-IDF v5.5.3（`D:\Espressif\esp-idf`）、Python `D:\Espressif\python_env\idf5.5_py3.11_env`
- 编译需 `IDF_CCACHE_ENABLE=0`（中文用户名下 ccache 会失败）
- 烧录（USB-Serial/JTAG，COM3）：
  ```
  python -m esptool --chip esp32s3 -p COM3 -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin \
    0xd000 ota_data_initial.bin 0x10000 fakepodnano.bin
  ```
- 产物同步到 `firmware/build/`

---

## 二、功能清单（本轮新增/改造）

| 模块 | 状态 | 关键实现位置 |
|---|---|---|
| 主页 2x2 宫格 + 顶栏时间/电量 | ✅ | `ui/ui_view_home.cpp`、`services/battery_service.cpp` |
| 音乐目录页（浏览/进入/返回/点播/曲目数量） | ✅ | `ui/ui_view_files.cpp` |
| 系统设置页（亮度6档/自动息屏/关于/均衡器/IMU开关） | ✅ | `ui/ui_view_settings.cpp` |
| 音量控制（±按钮 + 上下滑手势 + HUD） | ✅ | `ui/ui_view_player.cpp`、`audio/audio_pipeline.cpp` |
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
- 关闭后不再识别（开关目前存 RAM，重启恢复默认开）

### 系统设置
- 屏幕亮度 6 档（真实写 CO5300 亮度寄存器）
- 自动息屏（30秒/1分钟/3分钟/从不），息屏后触摸点亮
- 均衡器：关闭/流行/摇滚/古典/人声
- 关于本设备

### USB 声卡
- 进入「USB DAC」卡片/长按功能键才启动（开机不占用 USB）
- UAC2.0：44.1/48/88.2/96kHz、24bit-in-32、立体声（全速 USB 带宽上限 96k）

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
2. **MSC U盘模式**：按「独立固件变体」实现。`components/usb_msc_dev` 用纯 TinyUSB MSC 描述符 + sdmmc 扇区读写，把 TF 卡暴露为 U 盘。与 UAC 声卡**互斥**（Kconfig `FAKEPOD_USB_MSC_MODE`，默认关，默认固件不含）。构建 MSC 变体：
   ```
   # 准备配置（在 sdkconfig 基础上追加）
   #   CONFIG_FAKEPOD_USB_MSC_MODE=y  -> sdkconfig.msc
   idf.py -B fw-build-msc -DSDKCONFIG=<项目>/sdkconfig.msc build
   ```
   已编译通过（约 272KB），并实测其启动进入 MSC 路径（无卡时正确提示「TF 卡初始化失败」）。**插入 TF 卡后插电脑应出现可移动磁盘**，此步需人工验证。
3. **FAT 文件名**：已切 UTF-8 编码，但 FatFs 代码页仍为 437；极个别符号可能显示异常。
4. **测试**：全部功能未运行自动化测试，是否可用以人工验证为准。
5. 开机若插着 USB 调试，偶发 I2C 扫描毛刺（非功能性问题）。

---

## 五、已知待人工验证点

- MSC U盘：需插入 TF 卡后接电脑确认出现可移动磁盘（代码已编译并进入 MSC 路径）
- DSD(DoP)：需实测是否有声（依赖 DAC 是否支持 DoP / DSD 引脚）
- M4A：需实测可播放
- 陀螺仪「双击」灵敏度需按手感微调（`imu_service.cpp` 中的 `TAP_G` / `TILT_G`）
