<#
.SYNOPSIS
    Fresh-clone bring-up for the StepperMotorController repo on Windows.

.DESCRIPTION
    Run this once after `git clone` on a machine that has nothing installed. It
    - installs (or verifies) every tool with winget, per user, no administrator needed:
        Git.Git                           git                     clone, submodules
        Arm.GnuArmEmbeddedToolchain       arm-none-eabi-gcc/g++   firmware cross-compiler
        Kitware.CMake                     cmake                   build generator (firmware, tests, sim)
        Ninja-build.Ninja                 ninja                   build backend
        BrechtSanders.WinLibs.POSIX.UCRT  gcc/g++ (MinGW-w64)     host unit tests + virtual Nucleo
        Python.Python.3.12                python                  scripts, client, tools
        xpack-dev-tools.openocd-xpack     openocd                 flashing via ST-LINK
    - fetches the git submodules (FreeRTOS kernel under server/external/)
    - creates client\.venv with the client requirements (+ pytest), and optionally
      tools\chassis_cost\.venv (-WithChassisTool, about 640 MB)
    - QUEUES what it cannot do for you and prints the queue at the end: anything that
      needs administrator rights or a manual download (the ST-LINK USB driver, winget
      itself when it is missing, every tool when winget is missing). With -OpenLinks the
      download page of each queued item is opened in your browser.
    Safe to re-run: installed packages are skipped, existing venvs are reused.

.PARAMETER VerifyOnly
    Install nothing; report what is present and what would be queued.
.PARAMETER SkipFirmware
    Client-only machine: no cross-compiler, CMake, Ninja, host g++ or OpenOCD.
.PARAMETER SkipPython
    Do not install Python or create the virtual environments.
.PARAMETER SkipOpenOCD
    Do not install OpenOCD (flashing by copying the .bin to the NUCLEO drive still works).
.PARAMETER WithChassisTool
    Also create tools\chassis_cost\.venv (cadquery-ocp, large download).
.PARAMETER OpenLinks
    Open the download page of every queued manual item in the default browser.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\bringup.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\bringup.ps1 -VerifyOnly
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\setup\bringup.ps1 -SkipFirmware -OpenLinks
#>
[CmdletBinding()]
param(
    [switch]$VerifyOnly,
    [switch]$SkipFirmware,
    [switch]$SkipPython,
    [switch]$SkipOpenOCD,
    [switch]$WithChassisTool,
    [switch]$OpenLinks
)

$ErrorActionPreference = 'Continue'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manual = New-Object System.Collections.ArrayList     # items the user has to do (admin / download)
$rows = New-Object System.Collections.ArrayList       # verification table

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Add-Row($what, $status, $detail) {
    [void]$rows.Add([pscustomobject]@{ Item = $what; Status = $status; Detail = $detail })
}
function Add-Manual($what, $why, $url, $steps) {
    [void]$manual.Add([pscustomobject]@{ What = $what; Why = $why; Url = $url; Steps = $steps })
}
function Refresh-SessionPath { . (Join-Path $PSScriptRoot '..\build\env.ps1') }
function Get-RealCommand($exe) {
    # The Microsoft Store "python" alias is an app-execution stub, not an interpreter
    $cmd = Get-Command $exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notmatch 'WindowsApps') { return $cmd }
    return $null
}
function Get-Version($path) {
    try { ((& $path --version 2>&1 | Select-Object -First 1) -replace '\s+', ' ').Trim() } catch { '' }
}

# ---------------------------------------------------------------------------
# 1. Package list: winget id, executable to verify, and the manual download page
# ---------------------------------------------------------------------------
$packages = @(
    @{ Id = 'Git.Git'; Exe = 'git'; Role = 'git (clone, submodules)';
       Url = 'https://git-scm.com/download/win' }
)
if (-not $SkipFirmware) {
    $packages += @{ Id = 'Arm.GnuArmEmbeddedToolchain'; Exe = 'arm-none-eabi-gcc'; Role = 'firmware cross-compiler';
                    Url = 'https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads' }
    $packages += @{ Id = 'Kitware.CMake'; Exe = 'cmake'; Role = 'build generator';
                    Url = 'https://cmake.org/download/' }
    $packages += @{ Id = 'Ninja-build.Ninja'; Exe = 'ninja'; Role = 'build backend';
                    Url = 'https://github.com/ninja-build/ninja/releases' }
    $packages += @{ Id = 'BrechtSanders.WinLibs.POSIX.UCRT'; Exe = 'g++'; Role = 'host compiler (unit tests, virtual Nucleo)';
                    Url = 'https://winlibs.com/' }
    if (-not $SkipOpenOCD) {
        $packages += @{ Id = 'xpack-dev-tools.openocd-xpack'; Exe = 'openocd'; Role = 'flashing / debug via ST-LINK';
                        Url = 'https://github.com/xpack-dev-tools/openocd-xpack/releases' }
    }
}
if (-not $SkipPython) {
    $packages += @{ Id = 'Python.Python.3.12'; Exe = 'python'; Role = 'scripts, client, tools';
                    Url = 'https://www.python.org/downloads/windows/' }
}

# ---------------------------------------------------------------------------
# 2. winget (App Installer). Without it every missing tool becomes a manual item.
# ---------------------------------------------------------------------------
$haveWinget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
if (-not $haveWinget) {
    Add-Manual 'winget (App Installer)' 'needed to install the tools automatically; ships with Windows 10 1709+ / 11' `
        'https://aka.ms/getwinget' 'Install "App Installer" from the Microsoft Store (the link opens it), reopen the terminal, re-run this script.'
}

function Test-Installed($id) {
    $out = winget list --id $id --exact --accept-source-agreements 2>$null
    return ($LASTEXITCODE -eq 0 -and ($out | Select-String -SimpleMatch $id))
}

# ---------------------------------------------------------------------------
# 3. Tools: install with winget, or queue the download
# ---------------------------------------------------------------------------
Refresh-SessionPath
foreach ($p in $packages) {
    if (Get-RealCommand $p.Exe) { continue }
    if ($VerifyOnly) { continue }
    if ($haveWinget) {
        if (Test-Installed $p.Id) { Write-Step "$($p.Id) installed but not on PATH yet (reopen the terminal)"; continue }
        Write-Step "Installing $($p.Id) ($($p.Role))"
        $args = @('install', '--id', $p.Id, '--exact', '--silent', '--accept-package-agreements',
                  '--accept-source-agreements', '--disable-interactivity')
        if ($p.Id -eq 'Python.Python.3.12') {
            $args += @('--override', '/quiet InstallAllUsers=0 PrependPath=1 Include_test=0')
        }
        & winget @args
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "winget exited $LASTEXITCODE for $($p.Id)"
            Add-Manual $p.Role "winget could not install $($p.Id) (exit $LASTEXITCODE)" $p.Url `
                "Download and install from the page; a per-user install needs no administrator. Then reopen the terminal."
        }
        Refresh-SessionPath
    } else {
        Add-Manual $p.Role 'winget is not available on this machine' $p.Url `
            'Download and install from the page (choose "add to PATH" where offered), then reopen the terminal and re-run this script.'
    }
}
Refresh-SessionPath

foreach ($p in $packages) {
    $cmd = Get-RealCommand $p.Exe
    if ($cmd) { Add-Row $p.Exe 'OK' ((Get-Version $cmd.Source) + '  ' + $cmd.Source) }
    else      { Add-Row $p.Exe 'MISSING' "$($p.Role): not on PATH (just installed? open a NEW terminal, or . scripts\build\env.ps1)" }
}

# ---------------------------------------------------------------------------
# 4. Git submodules (FreeRTOS kernel). core.longpaths: the kernel has deep paths.
# ---------------------------------------------------------------------------
$freertos = Join-Path $repoRoot 'server\external\X_middlewares\Third_Party\FreeRTOS-Kernel'
$haveKernel = Test-Path (Join-Path $freertos 'tasks.c')
if (-not $haveKernel -and -not $VerifyOnly -and (Get-RealCommand 'git') -and (Test-Path (Join-Path $repoRoot '.git'))) {
    Write-Step 'git submodule update --init --recursive (FreeRTOS kernel)'
    & git -C $repoRoot -c core.longpaths=true submodule update --init --recursive
    $haveKernel = Test-Path (Join-Path $freertos 'tasks.c')
}
if ($haveKernel) { Add-Row 'FreeRTOS submodule' 'OK' $freertos }
else {
    Add-Row 'FreeRTOS submodule' 'MISSING' 'run: git -c core.longpaths=true submodule update --init --recursive'
    if (-not (Get-RealCommand 'git')) {
        Add-Manual 'FreeRTOS kernel (git submodule)' 'git is not available to fetch it' `
            'https://github.com/FreeRTOS/FreeRTOS-Kernel' 'After git is installed: git -c core.longpaths=true submodule update --init --recursive'
    }
}

# ---------------------------------------------------------------------------
# 5. Python virtual environments
# ---------------------------------------------------------------------------
function Get-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        foreach ($v in '-3.12', '-3.13', '-3.11', '-3') {
            $exe = & py $v -c "import sys; print(sys.executable)" 2>$null
            if ($LASTEXITCODE -eq 0 -and $exe) { return $exe.Trim() }
        }
    }
    $cmd = Get-RealCommand 'python'
    if ($cmd) { return $cmd.Source }
    return $null
}
function Ensure-Venv($dir, $requirements, $extra, $label) {
    $venvPy = Join-Path $dir '.venv\Scripts\python.exe'
    if (-not (Test-Path $venvPy)) {
        if ($VerifyOnly) { Add-Row $label 'MISSING' "re-run without -VerifyOnly to create $dir\.venv"; return }
        $base = Get-Python
        if (-not $base) { Add-Row $label 'MISSING' 'no usable Python (Store alias only?): install Python, reopen the terminal, re-run'; return }
        Write-Step "Creating $dir\.venv with $base"
        & $base -m venv (Join-Path $dir '.venv')
        if (-not (Test-Path $venvPy)) { Add-Row $label 'MISSING' 'venv creation failed'; return }
    }
    if (-not $VerifyOnly) {
        Write-Step "pip install -r $requirements $extra"
        & $venvPy -m pip install --quiet --disable-pip-version-check -r $requirements $extra
        if ($LASTEXITCODE -ne 0) { Add-Row $label 'MISSING' "pip install failed (see above); re-run after fixing"; return }
    }
    Add-Row $label 'OK' $venvPy
}
if (-not $SkipPython) {
    Ensure-Venv (Join-Path $repoRoot 'client') (Join-Path $repoRoot 'client\requirements.txt') 'pytest' 'client\.venv'
    if ($WithChassisTool) {
        Ensure-Venv (Join-Path $repoRoot 'tools\chassis_cost') (Join-Path $repoRoot 'tools\chassis_cost\requirements.txt') '' 'tools\chassis_cost\.venv'
    } else {
        Add-Row 'tools\chassis_cost\.venv' 'SKIPPED' 'optional: re-run with -WithChassisTool (about 640 MB)'
    }
}

# ---------------------------------------------------------------------------
# 6. ST-LINK USB driver (administrator). The NUCLEO's VCP and mass-storage interfaces work
#    with Windows' own drivers; the debug interface (OpenOCD, GDB) needs ST's driver.
# ---------------------------------------------------------------------------
if (-not $SkipFirmware) {
    $stlink = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
              Where-Object { $_.InstanceId -like 'USB\VID_0483&PID_374B&MI_00*' }
    if (-not $stlink) {
        Add-Row 'ST-LINK driver' 'UNKNOWN' 'no NUCLEO plugged in; plug it in and re-run with -VerifyOnly to check'
    } elseif ($stlink.Status -eq 'OK') {
        Add-Row 'ST-LINK driver' 'OK' $stlink.FriendlyName
    } else {
        Add-Row 'ST-LINK driver' 'MISSING' "$($stlink.FriendlyName): $($stlink.Problem) (serial port and drag-and-drop flashing still work)"
        Add-Manual 'ST-LINK USB driver (STSW-LINK009)' 'needs administrator rights; without it OpenOCD cannot open the debug interface' `
            'https://www.st.com/en/development-tools/stsw-link009.html' `
            'Download (free ST account), unzip, right-click dpinst_amd64.exe > Run as administrator, replug the NUCLEO. Then: scripts\build\flash.bat'
    }
}

# ---------------------------------------------------------------------------
# 7. Report
# ---------------------------------------------------------------------------
Write-Step 'Verification'
$rows | Format-Table -AutoSize -Wrap | Out-String -Width 160 | Write-Host

$missing = @($rows | Where-Object Status -eq 'MISSING')
if ($manual.Count -gt 0) {
    Write-Host "Queued for you ($($manual.Count) item(s) that need a download or administrator rights):" -ForegroundColor Yellow
    $i = 0
    foreach ($m in $manual) {
        $i++
        Write-Host "  $i. $($m.What)" -ForegroundColor Yellow
        Write-Host "     why:   $($m.Why)"
        Write-Host "     link:  $($m.Url)"
        Write-Host "     steps: $($m.Steps)"
        if ($OpenLinks) { Start-Process $m.Url }
    }
    if (-not $OpenLinks) { Write-Host "  (re-run with -OpenLinks to open these pages in the browser)" }
}

if ($missing.Count -eq 0 -and $manual.Count -eq 0) {
    Write-Host "Everything present." -ForegroundColor Green
    Write-Host "  firmware:  . scripts\build\env.ps1 ; cmake --preset firmware-debug ; cmake --build --preset firmware-debug"
    Write-Host "  GUI:       scripts\client\run_gui.bat     (or run_gui.bat --sim without a board)"
    Write-Host "  tests:     cd client ; .venv\Scripts\python.exe -m pytest"
    exit 0
}
if ($missing.Count -gt 0) {
    Write-Host "$($missing.Count) item(s) missing. If you just installed something, open a NEW terminal and run: scripts\setup\bringup.ps1 -VerifyOnly" -ForegroundColor Yellow
}
if ($manual.Count -gt 0) { exit 3 } else { exit 2 }
