@echo off
REM Start the motor-controller command line (smc) (client/smc_cli). First run creates client\.venv and
REM installs the requirements. Arguments pass through, e.g.:  run_cli.bat info --sim
setlocal
set "CLIENT=%~dp0..\..\client"
set "PY=%CLIENT%\.venv\Scripts\python.exe"
if not exist "%PY%" (
    echo Creating client\.venv and installing requirements ...
    py -3 -m venv "%CLIENT%\.venv" || python -m venv "%CLIENT%\.venv" || exit /b 1
    "%PY%" -m pip install -q -r "%CLIENT%\requirements.txt" || exit /b 1
)
cd /d "%CLIENT%"
"%PY%" -m smc_cli %*
