@echo off
rem Double-click: the robot link menu (SSH, the Pi's desktop, VS Code, setup). See docs\PI_SETUP.md.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0robot_link.ps1" %*
if errorlevel 1 pause
