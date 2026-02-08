@echo off
REM Build and flash script with OpenOCD — shows output and saves to build/flash_log.txt

setlocal

REM Add OpenOCD to PATH temporarily for this session
set "PATH=F:\OpenOCD\xpack-openocd-0.12.0-7-win32-x64\xpack-openocd-0.12.0-7\bin;%PATH%"

set "LOGFILE=%~dp0..\build\flash_log.txt"

REM Build and flash with real-time output visible to user
call "%~dp0build.bat" flash
set "RESULT=%ERRORLEVEL%"

echo.
if %RESULT% EQU 0 (
    echo Flash completed successfully.
) else (
    echo Flash FAILED.
)

endlocal
exit /b %RESULT%
