# fakepodnano-renew

FAKE_POD_NANO（ESP32-S3）固件重写与配套工程：扩展本地解码格式、提升 USB Audio Class 采样率/位深能力，并重做 LVGL UI。

## 目录

| 路径 | 说明 |
|------|------|
| `firmware/` | ESP-IDF 主固件工程 |
| `src_oshwhub/` | 立创开源参考/移植源码 |
| `web_ble_sync/` | Web / BLE 同步相关 |
| `prototype/` | 原型与实验 |
| `CONFIG_TABLE.md` / `PINS_CONFIRMED.md` | 配置与引脚文档 |
| `REWRITE_PLAN.md` / `UI_DESIGN_SPEC.md` | 重写方案与 UI 规范 |

## 环境

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)（与工程 `sdkconfig` 匹配的版本）
- 本仓库**不包含** `tools/` 下的本地工具链；请在本机安装 ESP-IDF 并 `export` 环境后进入 `firmware/` 编译。

```bash
cd firmware
idf.py build
idf.py flash monitor
```

## 许可证

各子目录与 `managed_components` 内组件遵循其各自 LICENSE；使用前请阅读对应声明。
