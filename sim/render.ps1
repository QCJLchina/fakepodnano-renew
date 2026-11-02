# =============================================================================
# sim/render.ps1 — build (incremental) and render firmware views to 460x460 PNG.
#
#   sim/render.ps1                      # all core views -> sim/out/<view>.png
#   sim/render.ps1 -View home           # only sim/out/home.png
#   sim/render.ps1 -View home,player    # several views
#   sim/render.ps1 -View about          # settings "关于本设备" screen (see README)
#   sim/render.ps1 -Before              # -> sim/out/before/<view>.png (baseline)
#   sim/render.ps1 -NoBuild             # skip cmake/ninja, just re-render
#   sim/render.ps1 -UacActive           # preview the UAC screen while streaming
#   sim/render.ps1 -ContactSheet        # also write a 3x2 overview PNG
#
# Output is always exactly 460x460 RGB with no window chrome.
#
# Concurrency: safe to run from several terminals at once. The build is
# serialized through an exclusive lock in %TEMP%; renders run a private copy of
# the renderer so they never block a relink.
# =============================================================================
[CmdletBinding()]
param(
    [string[]]$View = @('all'),
    [string]$OutDir = '',
    [switch]$Before,
    [switch]$NoBuild,
    [switch]$UacActive,
    [switch]$KeepBmp,
    [switch]$ContactSheet,
    [int]$TimeoutSec = 120
)

$ErrorActionPreference = 'Stop'

$simDir   = $PSScriptRoot
$buildDir = Join-Path $simDir 'build'
$exePath  = Join-Path $buildDir 'sim_preview.exe'
$coreViews    = @('home', 'player', 'files', 'settings', 'uac', 'usbmode')
$optionalViews = @('about')
$allViews     = $coreViews + $optionalViews

. (Join-Path $simDir 'sim_common.ps1')

# ---------------------------------------------------------------- output dir
if (-not $OutDir) {
    $OutDir = if ($Before) { Join-Path $simDir 'out\before' } else { Join-Path $simDir 'out' }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Get-Item -LiteralPath $OutDir).FullName

# --------------------------------------------------------------- resolve views
$selected = @()
foreach ($v in $View) {
    foreach ($piece in ($v -split ',')) {
        $name = $piece.Trim().ToLowerInvariant()
        if (-not $name) { continue }
        if ($name -eq 'all') { $selected += $coreViews; continue }
        if ($allViews -notcontains $name) {
            throw "unknown view '$name' (valid: all, $($allViews -join ', '))"
        }
        $selected += $name
    }
}
$selected = $selected | Select-Object -Unique

# ---------------------------------------------------------------------- build
if (-not $NoBuild) {
    & (Join-Path $simDir 'build.ps1')
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "renderer not found: $exePath (run sim/build.ps1 first)"
}

# Private copy: keeps renders parallel-safe while another teammate relinks.
$runExe = Copy-SimRendererForRun -ExePath $exePath

# -------------------------------------------------------------------- python
$python = Get-SimPython
if (-not $python) {
    throw 'python.exe (with Pillow) not found. Set SIM_PYTHON to its full path.'
}

# --------------------------------------------------------------- render loop
if ($UacActive) { $env:SIM_UAC_ACTIVE = '1' }

$bmp2png = @'
import os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src)
if im.mode != "RGB":
    im = im.convert("RGB")
if im.size != (460, 460):
    print("WARN: %s is %dx%d (expected 460x460)" % (src, im.size[0], im.size[1]), file=sys.stderr)
im.save(dst, "PNG", optimize=False)
print("%dx%d %d bytes" % (im.size[0], im.size[1], os.path.getsize(dst)))
'@
$pyScript = Join-Path $OutDir '_bmp2png.py'
Set-Content -LiteralPath $pyScript -Value $bmp2png -Encoding UTF8

$results = @()
$failures = @()

foreach ($name in $selected) {
    $bmp = Join-Path $OutDir "$name.bmp"
    $png = Join-Path $OutDir "$name.png"
    if (Test-Path -LiteralPath $bmp) { Remove-Item -LiteralPath $bmp -Force }

    Write-Host "[sim] rendering $name ..." -NoNewline

    # System.Diagnostics.Process (not Start-Process): Start-Process -PassThru
    # does not report ExitCode reliably in this PowerShell build.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $runExe
    $psi.Arguments = "$name `"$bmp`""
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        try { $proc.Kill() } catch { }
        Write-Host " TIMEOUT" -ForegroundColor Red
        $failures += "$name (timeout after ${TimeoutSec}s)"
        continue
    }
    $proc.WaitForExit()
    $code = $proc.ExitCode
    $stderrText = $stderrTask.Result

    if ($code -ne 0 -or -not (Test-Path -LiteralPath $bmp)) {
        Write-Host " FAILED (exit $code)" -ForegroundColor Red
        if ($stderrText) {
            $stderrText -split "`r?`n" | Select-Object -Last 15 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
        }
        $failures += "$name (renderer exit $code)"
        continue
    }

    $info = & $python $pyScript $bmp $png 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host " PNG CONVERT FAILED" -ForegroundColor Red
        $failures += "$name (png conversion failed)"
        continue
    }
    Write-Host " ok" -ForegroundColor Green
    if (-not $KeepBmp) { Remove-Item -LiteralPath $bmp -Force -ErrorAction SilentlyContinue }
    $results += [pscustomobject]@{
        View = $name
        Png  = $png
        Info = ($info | Select-Object -Last 1)
        Bytes = (Get-Item -LiteralPath $png).Length
    }
}

Remove-Item -LiteralPath $pyScript -Force -ErrorAction SilentlyContinue
if ($runExe -and (Test-Path -LiteralPath $runExe)) {
    Remove-Item -LiteralPath $runExe -Force -ErrorAction SilentlyContinue
}
foreach ($f in Get-ChildItem -LiteralPath $OutDir -Filter '_*' -File -ErrorAction SilentlyContinue |
                 Where-Object { $_.Extension -in @('.log', '.err') }) {
    Remove-Item -LiteralPath $f.FullName -Force
}

# -------------------------------------------------------------- contact sheet
if ($ContactSheet -and $results.Count -gt 0) {
    $sheet = Join-Path $OutDir 'contact_sheet.png'
    $sheetPy = Join-Path $OutDir '_sheet.py'
    $files = ($results | ForEach-Object { $_.Png }) -join "`n"
    $code = @'
import sys
from PIL import Image, ImageDraw
paths = [p for p in sys.argv[2].splitlines() if p.strip()]
out = sys.argv[1]
cols, pad = 3, 12
cell_w, cell_h = 460, 460 + 22
rows = (len(paths) + cols - 1) // cols
sheet = Image.new("RGB", (cols * cell_w + (cols + 1) * pad,
                          rows * (cell_h + pad) + pad), (24, 24, 28))
draw = ImageDraw.Draw(sheet)
for i, p in enumerate(paths):
    r, c = divmod(i, cols)
    x = pad + c * (cell_w + pad)
    y = pad + r * (cell_h + pad)
    im = Image.open(p).convert("RGB")
    sheet.paste(im, (x, y + 22))
    draw.text((x + 4, y + 4), p.rsplit("\\", 1)[-1].replace(".png", ""), fill=(230, 230, 235))
sheet.save(out, "PNG")
print(out)
'@
    Set-Content -LiteralPath $sheetPy -Value $code -Encoding UTF8
    & $python $sheetPy $sheet $files | Out-Null
    Remove-Item -LiteralPath $sheetPy -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $sheet) {
        Write-Host "[sim] contact sheet -> $sheet" -ForegroundColor Green
    }
}

# ------------------------------------------------------------------- manifest
# A "before" set is only useful if we can prove which sources produced it, so
# record hashes + builds alongside the screenshots.
if ($Before) {
    $manifest = Join-Path $OutDir 'MANIFEST.txt'
    $root = Split-Path $simDir -Parent
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("FAKE_POD_NANO sim/ baseline (BEFORE the view rewrite)")
    $lines.Add("generated : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')")
    $lines.Add("renderer  : $exePath")
    $lines.Add("exe sha256: $((Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash)")
    $gitHead = try { (& git -C $root rev-parse --short HEAD) 2>$null } catch { $null }
    if ($gitHead) { $lines.Add("git HEAD  : $gitHead") }
    $lines.Add('')
    $lines.Add('source files compiled at this moment:')
    $sources = @()
    $sources += Get-ChildItem -LiteralPath (Join-Path $root 'firmware\main\ui') -Filter 'ui_view_*.cpp' -File -ErrorAction SilentlyContinue
    foreach ($extra in @('ui_theme.cpp', 'ui_icons.c', 'ui_theme.h', 'ui_fonts.h', 'ui_icons.h',
                         'ui_manager.h', 'fonts\ui_font_cjk_16.c', 'fonts\ui_font_cjk_20.c')) {
        $p = Join-Path (Join-Path $root 'firmware\main\ui') $extra
        if (Test-Path -LiteralPath $p) { $sources += Get-Item -LiteralPath $p }
    }
    foreach ($s in ($sources | Sort-Object FullName -Unique)) {
        $hash = (Get-FileHash -LiteralPath $s.FullName -Algorithm SHA256).Hash.Substring(0, 16)
        $rel = $s.FullName.Substring($root.Length + 1)
        $lines.Add(('{0}  {1,9}  {2:yyyy-MM-dd HH:mm:ss}  {3}' -f $hash, $s.Length, $s.LastWriteTime, $rel))
    }
    $lines.Add('')
    $lines.Add('images:')
    foreach ($r in $results) {
        $lines.Add(('{0,-10} {1}' -f $r.View, (Split-Path $r.Png -Leaf)))
    }
    Set-Content -LiteralPath $manifest -Value $lines -Encoding UTF8
    Write-Host "[sim] baseline manifest -> $manifest" -ForegroundColor Green
}

# -------------------------------------------------------------------- summary
if ($results.Count -gt 0) {
    Write-Host ''
    $results | Format-Table -AutoSize View, @{ n = 'Size'; e = { $_.Info } }, @{ n = 'PNG'; e = { $_.Png } } | Out-String | Write-Host
}

if ($failures.Count -gt 0) {
    Write-Host "[sim] views that could NOT be rendered:" -ForegroundColor Yellow
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    Write-Host "[sim] (if the error is inside firmware/main/ui/ui_view_*.cpp it is a source error, not a harness error)" -ForegroundColor DarkGray
    exit 1
}

Write-Host ("[sim] done: {0} view(s) in {1}" -f $results.Count, $OutDir) -ForegroundColor Green
