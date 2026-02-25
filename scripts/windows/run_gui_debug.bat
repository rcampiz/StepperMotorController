@echo off
REM Launch GUI in debug mode — all log output goes to terminal
setlocal

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "CLIENT_SRC=%PROJECT_ROOT%\client\src"

echo ========================================
echo STM32 Motor Controller GUI (Debug Mode)
echo ========================================
echo.

REM Activate venv if present
if exist "%PROJECT_ROOT%\client\.venv\Scripts\activate.bat" (
    call "%PROJECT_ROOT%\client\.venv\Scripts\activate.bat"
)

REM Install/update dependencies from requirements.txt
if exist "%PROJECT_ROOT%\client\requirements.txt" (
    pip install -q -r "%PROJECT_ROOT%\client\requirements.txt"
)

pushd "%CLIENT_SRC%"
echo Working directory: %CD%
echo.
python gui_main.py --debug
set "EXIT_CODE=%ERRORLEVEL%"

if %EXIT_CODE% NEQ 0 (
    echo.
    echo === GUI exited with error code %EXIT_CODE% ===
)
popd

endlocal
echo.
pause
