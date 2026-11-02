# =============================================================================
# sim/build.ps1 — incremental MSVC + Ninja build of the host preview renderer.
#
#   sim/build.ps1              # configure + build (incremental)
#   sim/build.ps1 -Clean       # wipe sim/build first
#   sim/build.ps1 -Reconfigure # force a cmake configure pass
#
# The whole build is protected by an exclusive lock in %TEMP% so that several
# teammates calling render.ps1 at the same time queue instead of corrupting the
# build directory. Everything is written under sim/ only.
# Prerequisites are auto-detected; set SIM_CMAKE / SIM_NINJA / SIM_VCVARS.
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Reconfigure,
    [int]$LockTimeoutSec = 600
)

$ErrorActionPreference = 'Stop'

$simDir   = $PSScriptRoot
$buildDir = Join-Path $simDir 'build'
$exePath  = Join-Path $buildDir 'sim_preview.exe'

. (Join-Path $simDir 'sim_common.ps1')

$cmake = Find-SimTool -EnvOverride $env:SIM_CMAKE `
    -Candidates @(
        'C:\Users\MAXSUN\.espressif\tools\cmake\3.30.2\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    ) `
    -GlobPatterns @('C:\Users\MAXSUN\.espressif\tools\cmake\*\bin\cmake.exe') `
    -CommandName 'cmake'

$ninja = Find-SimTool -EnvOverride $env:SIM_NINJA `
    -GlobPatterns @('C:\Users\MAXSUN\.espressif\tools\ninja\*\ninja.exe') `
    -CommandName 'ninja'

$vcvars = Find-SimTool -EnvOverride $env:SIM_VCVARS `
    -Candidates @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    ) `
    -CommandName 'vcvars64.bat'

if (-not $cmake) { throw 'cmake.exe not found. Set SIM_CMAKE to its full path.' }
if (-not $ninja) { throw 'ninja.exe not found. Set SIM_NINJA to its full path.' }
if (-not $vcvars) { throw 'vcvars64.bat not found. Set SIM_VCVARS to its full path.' }

# ------------------------------------------------------------------- the lock
# Everything from -Clean to the final link happens while holding the lock, so
# two teammates can never configure/compile into the same build dir at once.
$lock = Get-SimBuildLock -TimeoutSec $LockTimeoutSec
try {
    if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
        Write-Host "[sim] removing $buildDir" -ForegroundColor DarkGray
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    # Run everything through a generated .bat so paths with spaces (the
    # workspace path contains one) never meet cmd.exe quoting rules.
    $batPath = Join-Path $buildDir '_sim_build.bat'
    $reconfigureFlag = if ($Reconfigure) { '--fresh' } else { '' }
    $bat = @"
@echo off
rem VSLANG=1033 forces English MSVC diagnostics. Ninja parses /showIncludes
rem output ("Note: including file:") to build the header dependency graph; with
rem a localized (e.g. Chinese) compiler that parse fails and header edits would
rem silently not trigger rebuilds.
set VSLANG=1033
call "$vcvars" >nul 2>&1
if errorlevel 1 ( echo [sim] vcvars64.bat failed & exit /b 1 )
"$cmake" -S "$simDir" -B "$buildDir" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release $reconfigureFlag
if errorlevel 1 exit /b 1
"$cmake" --build "$buildDir" --parallel
exit /b %errorlevel%
"@
    Set-Content -LiteralPath $batPath -Value $bat -Encoding ASCII

    Write-Host "[sim] cmake : $cmake"
    Write-Host "[sim] ninja : $ninja"
    Write-Host "[sim] msvc  : $vcvars"
    Write-Host "[sim] build : $buildDir"

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & cmd.exe /c $batPath
    $code = $LASTEXITCODE
    $sw.Stop()

    if ($code -ne 0) {
        throw "sim build failed (exit $code). See $batPath and the compiler output above."
    }

    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "sim build reported success but $exePath is missing."
    }

    Write-Host ("[sim] built {0} in {1:n1}s" -f $exePath, $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}
finally {
    Close-SimBuildLock -Lock $lock
}
