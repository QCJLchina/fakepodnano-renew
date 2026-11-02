# FAKE_POD_NANO firmware build (Windows / ESP-IDF v5.5, E:\espressif\esp-idf)
# Usage: pwsh -File scripts\build_fw.ps1 [-Quiet]
# Notes:
#   - IDF_SKIP_CHECK_SUBMODULES=1 keeps cmake from trying to git-init esp-idf submodules.
#   - Runs one build at a time (lock file) because concurrent ninja runs corrupt build/.
param([switch]$Quiet)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $root 'firmware'
$lockPath = Join-Path $env:TEMP 'fakepodnano_build.lock'
$log = Join-Path $env:TEMP 'fakepodnano_build.log'

# ---- single-instance lock ----
$deadline = (Get-Date).AddMinutes(30)
while ($true) {
    try {
        $fs = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
        break
    } catch {
        if ((Get-Date) -gt $deadline) { Write-Error "build lock timeout: $lockPath"; exit 2 }
        Start-Sleep -Seconds 3
    }
}

try {
    $cmd = @"
call "E:\espressif\esp-idf\export.bat" >nul 2>&1
set IDF_SKIP_CHECK_SUBMODULES=1
cd /d "$fw"
idf.py build
"@
    $bat = Join-Path $env:TEMP 'fakepodnano_build.cmd'
    Set-Content -Path $bat -Value $cmd -Encoding ASCII
    Write-Host "== building $fw (log: $log) =="
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$bat`"" -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $out = Get-Content $log -ErrorAction SilentlyContinue
    $err = Get-Content "$log.err" -ErrorAction SilentlyContinue
    $errors = @($out + $err) | Select-String -Pattern 'error:|FAILED|undefined reference' | Select-Object -First 40
    if (-not $Quiet) {
        Write-Host "== last 25 lines =="
        @($out + $err) | Select-Object -Last 25
    }
    if ($p.ExitCode -ne 0 -or $errors) {
        Write-Host "== FAILED (exit $($p.ExitCode)) =="
        $errors | ForEach-Object { $_.Line }
        exit 1
    }
    $size = (Get-Item (Join-Path $fw 'build\fakepodnano.bin') -ErrorAction SilentlyContinue).Length
    Write-Host "== OK == fakepodnano.bin = $size bytes"
    exit 0
}
finally {
    if ($fs) { $fs.Close(); $fs.Dispose() }
    Remove-Item $lockPath -Force -ErrorAction SilentlyContinue
}
