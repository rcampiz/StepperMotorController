@echo off
rem Double-click: straight to the Pi's desktop (VNC); the one-time setup first if it has not run.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0robot_link.ps1" -Action desktop
if errorlevel 1 pause
