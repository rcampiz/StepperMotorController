# Start GUI and CLI client together (Windows PowerShell)
# Usage:
#   .\start_gui_and_client.ps1                 # GUI + CLI (interactive)
#   .\start_gui_and_client.ps1 --gui-only      # GUI only
#   .\start_gui_and_client.ps1 --cli-only      # CLI only
#   .\start_gui_and_client.ps1 -c "GET_STATUS" # GUI + single CLI command

param(
    [switch]$GuiOnly,
    [switch]$CliOnly,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$ClientArgs
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RunClient = Join-Path $ScriptDir "run_client.ps1"

if (-not (Test-Path $RunClient)) {
    Write-Host "ERROR: run_client.ps1 not found in $ScriptDir" -ForegroundColor Red
    exit 1
}

if (-not $CliOnly) {
    Write-Host "Starting GUI..." -ForegroundColor Yellow
    Start-Process powershell -ArgumentList @(
        "-ExecutionPolicy", "Bypass",
        "-File", $RunClient,
        "-Gui"
    )
}

if (-not $GuiOnly) {
    Write-Host "Starting CLI..." -ForegroundColor Yellow
    if ($ClientArgs) {
        & $RunClient @ClientArgs
    } else {
        & $RunClient
    }
}
