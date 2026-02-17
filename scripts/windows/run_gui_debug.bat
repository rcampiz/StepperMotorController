@echo off
REM Launch GUI in debug mode — all log output goes to terminal and log file
setlocal

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "CLIENT_SRC=%PROJECT_ROOT%\client\src"

REM Activate venv if present
if exist "%PROJECT_ROOT%\client\.venv\Scripts\activate.bat" (
    call "%PROJECT_ROOT%\client\.venv\Scripts\activate.bat"
)

pushd "%CLIENT_SRC%"
python gui_main.py --debug 2> gui_crash.log
if errorlevel 1 (
    echo.
    echo === CRASH LOG ===
    type gui_crash.log
    echo.
)
popd

endlocal
pause
