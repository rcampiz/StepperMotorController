@echo off
REM Build and flash script with OpenOCD — shows output and saves to build/flash_log.txt

setlocal

REM OpenOCD: use the one on PATH (installed by scripts\bringup.ps1); fall back to the legacy F: drive copy
where openocd >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    if exist "F:\OpenOCD\xpack-openocd-0.12.0-7-win32-x64\xpack-openocd-0.12.0-7\bin\openocd.exe" (
        set "PATH=F:\OpenOCD\xpack-openocd-0.12.0-7-win32-x64\xpack-openocd-0.12.0-7\bin;%PATH%"
    ) else (
        echo ERROR: openocd not found. Run: powershell -ExecutionPolicy Bypass -File scripts\bringup.ps1
        exit /b 1
    )
)

set "LOGFILE=%~dp0..\build\flash_log.txt"

REM Build and flash with real-time output visible to user
call "%~dp0build_direct.bat" flash
set "RESULT=%ERRORLEVEL%"

echo.
if %RESULT% EQU 0 (
    echo Flash completed successfully.
) else (
    echo Flash FAILED.
)

endlocal
exit /b %RESULT%
