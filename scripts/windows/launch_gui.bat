@echo off
REM Launch STM32 Motor Controller GUI
REM Double-click this file to start the GUI

setlocal

REM Get script directory and project root
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "CLIENT_DIR=%PROJECT_ROOT%\client"
set "VENV_DIR=%CLIENT_DIR%\.venv"
set "REQUIREMENTS=%CLIENT_DIR%\requirements.txt"

echo ========================================
echo STM32 Motor Controller GUI
echo ========================================
echo.

REM Find Python - check multiple locations
set "PYTHON="

REM Check if already in venv
if exist "%VENV_DIR%\Scripts\python.exe" (
    set "PYTHON=%VENV_DIR%\Scripts\python.exe"
    goto :found_python
)

REM Check PATH
where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "PYTHON=python"
    goto :found_python
)

REM Check py launcher (comes with Python installer)
where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "PYTHON=py"
    goto :found_python
)

REM Check common install locations
for %%P in (
    "%LOCALAPPDATA%\Programs\Python\Python314\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
    "%LOCALAPPDATA%\Programs\Python\Python310\python.exe"
    "C:\Python314\python.exe"
    "C:\Python313\python.exe"
    "C:\Python312\python.exe"
    "%LOCALAPPDATA%\Microsoft\WindowsApps\python.exe"
) do (
    if exist %%P (
        set "PYTHON=%%~P"
        goto :found_python
    )
)

echo ERROR: Python not found!
echo.
echo Please install Python 3.10+ from https://python.org
echo Make sure to check "Add Python to PATH" during installation.
pause
exit /b 1

:found_python
echo Found Python: %PYTHON%

REM Create venv if needed
if not exist "%VENV_DIR%\Scripts\python.exe" (
    echo Creating virtual environment...
    %PYTHON% -m venv "%VENV_DIR%"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to create venv
        pause
        exit /b 1
    )
)

REM Upgrade pip first (suppress warnings)
"%VENV_DIR%\Scripts\python.exe" -m pip install --upgrade pip -q >nul 2>&1

REM Install dependencies (show output to see what's happening)
echo Installing dependencies...
"%VENV_DIR%\Scripts\pip.exe" install -r "%REQUIREMENTS%"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install dependencies
    pause
    exit /b 1
)
echo Dependencies OK

REM Launch GUI
echo.
echo Starting GUI...
"%VENV_DIR%\Scripts\python.exe" "%CLIENT_DIR%\src\main.py" --gui

endlocal
