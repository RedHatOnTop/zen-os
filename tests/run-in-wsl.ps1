# =====================================================================
#  run-in-wsl.ps1 — WSL2 Test Runner for Zen OS
#
#  Thin PowerShell wrapper that forwards bash test scripts to WSL2
#  Ubuntu where QEMU/KVM are available.
#
#  Usage:
#    .\tests\run-in-wsl.ps1 <test-script> [args...]
#
#  Examples:
#    .\tests\run-in-wsl.ps1 tests/scenefx-renderer/test_renderer_assertion.sh
#    .\tests\run-in-wsl.ps1 tests/scenefx-renderer/test_renderer_preservation.sh
#
#  Environment variables (optional, forwarded to WSL):
#    ZEN_OS_IMAGE       Path to test disk image (Windows or WSL path)
#    ZEN_LOG_DIR        Log output directory
#    ZEN_BOOT_TIMEOUT   Boot wait timeout in seconds
# =====================================================================

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$TestScript,

    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

# ── Resolve WSL distro ───────────────────────────────────────────────
$WslDistro = "Ubuntu"

# Verify WSL distro is available.
$distros = wsl -l -q 2>$null
if ($LASTEXITCODE -ne 0 -or -not ($distros -match $WslDistro)) {
    Write-Error "WSL distro '$WslDistro' not found. Install it with: wsl --install -d Ubuntu"
    exit 1
}

# ── Convert Windows repo root to WSL path ────────────────────────────
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$WslRepoRoot = wsl -d $WslDistro -- wslpath "$RepoRoot" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to convert repo path to WSL: $RepoRoot"
    exit 1
}

# Convert test script to WSL-relative path.
$WslTestScript = "$WslRepoRoot/$($TestScript -replace '\\','/')"

# ── Build environment forwarding ─────────────────────────────────────
$EnvExports = @()

# Forward ZEN_* environment variables if set.
foreach ($var in @("ZEN_OS_IMAGE", "ZEN_LOG_DIR", "ZEN_BOOT_TIMEOUT")) {
    $val = [System.Environment]::GetEnvironmentVariable($var)
    if ($val) {
        # Convert Windows paths to WSL paths.
        if ($val -match '^[A-Za-z]:\\') {
            $val = wsl -d $WslDistro -- wslpath "$val" 2>$null
        }
        $EnvExports += "export $var='$val';"
    }
}

$EnvString = $EnvExports -join " "

# ── Build command ────────────────────────────────────────────────────
$ArgsString = ""
if ($ExtraArgs) {
    $ArgsString = " " + ($ExtraArgs -join " ")
}

$BashCmd = "${EnvString} cd '$WslRepoRoot' && bash '$WslTestScript'${ArgsString}"

Write-Host "[run-in-wsl] Distro:  $WslDistro" -ForegroundColor Cyan
Write-Host "[run-in-wsl] Repo:    $WslRepoRoot" -ForegroundColor Cyan
Write-Host "[run-in-wsl] Script:  $TestScript" -ForegroundColor Cyan
Write-Host ""

# ── Execute in WSL ───────────────────────────────────────────────────
wsl -d $WslDistro -- bash -c $BashCmd
$ExitCode = $LASTEXITCODE

if ($ExitCode -eq 0) {
    Write-Host ""
    Write-Host "[run-in-wsl] PASSED" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[run-in-wsl] FAILED (exit code: $ExitCode)" -ForegroundColor Red
}

exit $ExitCode
