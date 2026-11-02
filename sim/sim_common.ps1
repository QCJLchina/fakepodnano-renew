# =============================================================================
# sim/sim_common.ps1 — helpers shared by build.ps1 / render.ps1
# Dot-sourced, not executed directly.
#
# Concurrency model (5 view teammates call render.ps1 at the same time):
#   * The BUILD phase is serialized with an exclusive lock file in %TEMP%.
#     Two concurrent `cmake --build` runs in the same build dir would corrupt
#     the output / fight over the .exe, so they queue instead of failing.
#   * The RENDER phase is fully parallel: render.ps1 runs a private copy of
#     sim_preview.exe in %TEMP%, so a relink by another teammate can never
#     collide with a running renderer ("LNK1168: cannot open ... for writing").
#   * A process that dies while holding the lock releases it automatically,
#     because the lock is a live FileShare.None handle (no stale-lock cleanup).
# =============================================================================

function Get-SimBuildLock {
    param(
        [int]$TimeoutSec = 600,
        [string]$LockName = 'fakepodnano_sim_build.lock'
    )

    $lockPath = Join-Path ([System.IO.Path]::GetTempPath()) $LockName
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $announced = $false

    while ($true) {
        try {
            $stream = [System.IO.File]::Open(
                $lockPath,
                [System.IO.FileMode]::OpenOrCreate,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $payload = [System.Text.Encoding]::UTF8.GetBytes(
                "pid=$PID started=$(Get-Date -Format o)")
            $stream.SetLength(0)
            $stream.Write($payload, 0, $payload.Length)
            $stream.Flush()
            return @{ Stream = $stream; Path = $lockPath }
        }
        catch [System.IO.IOException] {
            if (-not $announced) {
                Write-Host "[sim] another teammate is building; queued on the build lock..." -ForegroundColor DarkGray
                $announced = $true
            }
            if ((Get-Date) -gt $deadline) {
                throw "timed out after ${TimeoutSec}s waiting for the sim build lock: $lockPath"
            }
            Start-Sleep -Milliseconds 200
        }
        catch [System.UnauthorizedAccessException] {
            if ((Get-Date) -gt $deadline) {
                throw "access denied on the sim build lock: $lockPath"
            }
            Start-Sleep -Milliseconds 200
        }
    }
}

function Close-SimBuildLock {
    param($Lock)
    if ($Lock -and $Lock.Stream) {
        try { $Lock.Stream.Dispose() } catch { }
    }
}

function Find-SimTool {
    param(
        [string]$EnvOverride,
        [string[]]$Candidates = @(),
        [string[]]$GlobPatterns = @(),
        [string]$CommandName
    )
    if ($EnvOverride -and (Test-Path -LiteralPath $EnvOverride)) { return $EnvOverride }
    foreach ($c in $Candidates) { if ($c -and (Test-Path -LiteralPath $c)) { return $c } }
    foreach ($g in $GlobPatterns) {
        $hit = Get-ChildItem -Path $g -ErrorAction SilentlyContinue |
               Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-SimPython {
    $candidates = @(
        $env:SIM_PYTHON,
        'C:\Users\MAXSUN\AppData\Local\Python\pythoncore-3.14-64\python.exe',
        'C:\Users\MAXSUN\AppData\Local\Programs\Python\Python313\python.exe'
    )
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return $c }
    }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command python3 -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Private copy of the renderer so concurrent builds can relink the real exe.
function Copy-SimRendererForRun {
    param(
        [string]$ExePath,
        [int]$TimeoutSec = 600
    )
    $lock = Get-SimBuildLock -TimeoutSec $TimeoutSec
    try {
        $runDir = Join-Path ([System.IO.Path]::GetTempPath()) 'fakepodnano_sim_run'
        New-Item -ItemType Directory -Force -Path $runDir | Out-Null
        # opportunistic cleanup of copies left behind by killed runs
        Get-ChildItem -LiteralPath $runDir -Filter 'sim_preview_*.exe' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -lt (Get-Date).AddHours(-2) } |
            Remove-Item -Force -ErrorAction SilentlyContinue
        $runExe = Join-Path $runDir ("sim_preview_{0}.exe" -f $PID)
        Copy-Item -LiteralPath $ExePath -Destination $runExe -Force
        return $runExe
    }
    finally {
        Close-SimBuildLock -Lock $lock
    }
}
