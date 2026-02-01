@echo off
REM Temporary script to flash with OpenOCD without modifying system PATH

REM Add OpenOCD to PATH temporarily for this session
set "PATH=F:\OpenOCD\xpack-openocd-0.12.0-7-win32-x64\xpack-openocd-0.12.0-7\bin;%PATH%"

REM Call the main build script with flash target
call "%~dp0build.bat" flash