# FAKE_POD_NANO 一键烧录脚本
param(
    [string]$Port = "COM3",
    [int]$Baud = 921600
)

$ErrorActionPreference = "Stop"

$env:IDF_PATH = "E:\espressif\esp-idf"
$python = "C:\Users\MAXSUN\.espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe"
$firmwareDir = "E:\test astra\fakepodnano\firmware"

Set-Location $firmwareDir

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "   FAKE_POD_NANO 全新固件一键烧录程序" -ForegroundColor Cyan
Write-Host "   目标端口: $Port  波特率: $Baud" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

& $python -m esptool --chip esp32s3 -p $Port -b $Baud `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x0   "build\bootloader\bootloader.bin" `
    0x8000 "build\partition_table\partition-table.bin" `
    0xd000 "build\ota_data_initial.bin" `
    0x10000 "build\fakepodnano.bin"

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n[OK] 全新固件烧录成功！设备已自动重启进入新系统！" -ForegroundColor Green
} else {
    Write-Host "`n[FAIL] 烧录出现异常，请确认串口未被其他助手占用。" -ForegroundColor Red
}
