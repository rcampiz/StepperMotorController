<#
.SYNOPSIS
    One-command developer bring-up for the StepperMotorController repo on Windows.

.DESCRIPTION
    Installs (or verifies) every tool the firmware, host-side unit tests, flashing,
    and the Python client need, using winget. Safe to re-run: already-installed
    packages are skipped. Refreshes PATH for the current session so the tools are
    usable immediately, then prints a verification table.

    Tools installed
      Arm.GnuArmEmbeddedToolchain       arm-none-eabi-gcc/g++   firmware cross-compiler
      Kitware.CMake                     cmake                   build generator (firmware + tests)
      Ninja-build.Ninja                 ninja                   fast build backend
      BrechtSanders.WinLibs.POSIX.UCRT  gcc/g++ (MinGW-w64)     host compiler for unit tests
      Python.Python.3.12                python                  check_*.py scripts + GUI client
      xpack-dev-tools.openocd-xpack     openocd                 flashing via ST-LINK

.PARAMETER SkipPython
    Do not install Python or the client's pip requirements.
.PARAMETER SkipOpenOCD
    Do not install OpenOCD (useful on build-only machines).
.PARAMETER VerifyOnly
    Do not install anything; just report what is present.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\bringup.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipPython,
    [switch]$SkipOpenOCD,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Continue'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# ---------------------------------------------------------------------------
# Package list: winget id -> executable used to verify it
# ---------------------------------------------------------------------------
$packages = @(
    @{ Id = 'Arm.GnuArmEmbeddedToolchain';      Exe = 'arm-none-eabi-gcc'; Role = 'firmware cross-compiler' },
    @{ Id = 'Kitware.CMake';                    Exe = 'cmake';             Role = 'build generator' },
    @{ Id = 'Ninja-build.Ninja';                Exe = 'ninja';             Role = 'build backend' },
    @{ Id = 'BrechtSanders.WinLibs.POSIX.UCRT'; Exe = 'g++';               Role = 'host compiler (unit tests)' }
)
if (-not $SkipPython)  { $packages += @{ Id = 'Python.Python.3.12';            Exe = 'python';  Role = 'scripts + client' } }
if (-not $SkipOpenOCD) { $packages += @{ Id = 'xpack-dev-tools.openocd-xpack'; Exe = 'openocd'; Role = 'flashing (ST-LINK)' } }

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

function Refresh-SessionPath {
    # Shared with interactive use: `. scripts\build\env.ps1`
    . (Join-Path $PSScriptRoot '..\build\env.ps1')
}

function Test-Installed($id) {
    $out = winget list --id $id --exact --accept-source-agreements 2>$null
    return ($LASTEXITCODE -eq 0 -and ($out | Select-String -SimpleMatch $id))
}

# ---------------------------------------------------------------------------
# 1. winget must exist (ships with Windows 10 1709+ / Windows 11 via App Installer)
# ---------------------------------------------------------------------------
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error "winget not found. Install 'App Installer' from the Microsoft Store, then re-run."
    exit 1
}

# ---------------------------------------------------------------------------
# 2. Install missing packages (sequentially: MSI installers refuse to run concurrently)
# ---------------------------------------------------------------------------
if (-not $VerifyOnly) {
    foreach ($p in $packages) {
        if (Test-Installed $p.Id) { Write-Step "$($p.Id) already installed"; continue }
        Write-Step "Installing $($p.Id) ($($p.Role))"
        $args = @('install', '--id', $p.Id, '--exact', '--silent', '--accept-package-agreements',
                  '--accept-source-agreements', '--disable-interactivity')
        if ($p.Id -eq 'Python.Python.3.12') {
            # per-user install, on PATH, no test suite
            $args += @('--override', '/quiet InstallAllUsers=0 PrependPath=1 Include_test=0')
        }
        & winget @args
        if ($LASTEXITCODE -ne 0) { Write-Warning "winget exited $LASTEXITCODE for $($p.Id); continuing" }
    }
}

Refresh-SessionPath

# ---------------------------------------------------------------------------
# 3. Python client requirements
# ---------------------------------------------------------------------------
if (-not $SkipPython -and -not $VerifyOnly) {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py -and $py.Source -notmatch 'WindowsApps') {
        Write-Step "pip install -r client/requirements.txt (+ pytest for client/tests)"
        & python -m pip install --quiet --disable-pip-version-check -r (Join-Path $repoRoot 'client\requirements.txt') pytest
    } else {
        Write-Warning "python not usable in this session yet (Store alias shadowing?). Reopen the terminal and re-run with -VerifyOnly."
    }
}

# ---------------------------------------------------------------------------
# 4. Verify
# ---------------------------------------------------------------------------
Write-Step "Verification"
$rows = foreach ($p in $packages) {
    $cmd = Get-Command $p.Exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notmatch 'WindowsApps') {
        $ver = (& $cmd.Source --version 2>&1 | Select-Object -First 1) -replace '\s+', ' '
        [pscustomobject]@{ Tool = $p.Exe; Status = 'OK'; Version = $ver; Path = $cmd.Source }
    } else {
        [pscustomobject]@{ Tool = $p.Exe; Status = 'MISSING'; Version = ''; Path = '(not on PATH; reopen terminal or install failed)' }
    }
}
$rows | Format-Table -AutoSize | Out-String | Write-Host

$missing = @($rows | Where-Object Status -eq 'MISSING')
if ($missing.Count -eq 0) {
    Write-Host "All tools present. Build the firmware with:  scripts\build\build_direct.bat" -ForegroundColor Green
    Write-Host "Flash with:                                  scripts\build\flash.bat" -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($missing.Count) tool(s) missing. If you just installed, open a NEW terminal and run: scripts\setup\bringup.ps1 -VerifyOnly" -ForegroundColor Yellow
    exit 2
}
