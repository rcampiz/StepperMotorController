# Launch GUI in debug mode — all log output goes to terminal
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path "$scriptDir\..\..").Path
$clientSrc = "$projectRoot\client\src"

# Activate venv if present
$venvActivate = "$projectRoot\client\.venv\Scripts\Activate.ps1"
if (Test-Path $venvActivate) {
    & $venvActivate
}

Push-Location $clientSrc
python gui_main.py --debug 2>&1 | Tee-Object -FilePath "$projectRoot\build\gui_debug.log"
Pop-Location
