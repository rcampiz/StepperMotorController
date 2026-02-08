# STM32 Motor Controller Client Launcher (Windows PowerShell)
# Usage: .\run_client.ps1 [arguments]
# Example: .\run_client.ps1 --gui              Launch GUI
# Example: .\run_client.ps1 -p COM3            Connect to COM3 (CLI)
# Example: .\run_client.ps1 --list             List serial ports
# Example: .\run_client.ps1 --json -c "GET_STATUS"

param(
    [switch]$InstallOnly,
    [switch]$Gui,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ClientArgs
)

$ErrorActionPreference = "Stop"

# Get script and project directories
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$ClientDir = Join-Path $ProjectRoot "client"
$VenvDir = Join-Path $ClientDir ".venv"
$RequirementsFile = Join-Path $ClientDir "requirements.txt"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "STM32 Motor Controller Client" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check Python version
Write-Host "Checking Python version..." -ForegroundColor Yellow
try {
    $pythonVersion = python --version 2>&1
    Write-Host "  Found: $pythonVersion" -ForegroundColor Green

    # Extract version number
    if ($pythonVersion -match "Python (\d+)\.(\d+)") {
        $major = [int]$Matches[1]
        $minor = [int]$Matches[2]
        if ($major -lt 3 -or ($major -eq 3 -and $minor -lt 10)) {
            Write-Host "  ERROR: Python 3.10+ required" -ForegroundColor Red
            exit 1
        }
    }
} catch {
    Write-Host "  ERROR: Python not found. Please install Python 3.10+" -ForegroundColor Red
    exit 1
}

# Create virtual environment if needed
if (-not (Test-Path $VenvDir)) {
    Write-Host ""
    Write-Host "Creating virtual environment..." -ForegroundColor Yellow
    python -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ERROR: Failed to create virtual environment" -ForegroundColor Red
        exit 1
    }
    Write-Host "  Created: $VenvDir" -ForegroundColor Green
}

# Activate virtual environment
$ActivateScript = Join-Path $VenvDir "Scripts\Activate.ps1"
Write-Host ""
Write-Host "Activating virtual environment..." -ForegroundColor Yellow
. $ActivateScript

# Install/update dependencies
Write-Host ""
Write-Host "Checking dependencies..." -ForegroundColor Yellow
pip install -r $RequirementsFile --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Failed to install dependencies" -ForegroundColor Red
    exit 1
}
Write-Host "  Dependencies OK" -ForegroundColor Green

# Exit if install-only mode
if ($InstallOnly) {
    Write-Host ""
    Write-Host "Installation complete." -ForegroundColor Green
    exit 0
}

# Run the client
Write-Host ""
Write-Host "Starting client..." -ForegroundColor Yellow
Write-Host "----------------------------------------"
$MainScript = Join-Path $ClientDir "src\main.py"

# Build arguments
$allArgs = @()
if ($Gui) {
    $allArgs += "--gui"
}
if ($ClientArgs) {
    $allArgs += $ClientArgs
}

python $MainScript @allArgs
$exitCode = $LASTEXITCODE

Write-Host "----------------------------------------"
Write-Host "Client exited with code: $exitCode"
exit $exitCode
