@echo off
REM Legacy entry point. CMake is the authoritative build (see CMakeLists.txt / CMakePresets.json);
REM this wrapper keeps the old command line working:
REM   scripts\build_direct.bat         -> configure + build firmware-debug into build\
REM   scripts\build_direct.bat flash   -> ... then flash with OpenOCD (used by scripts\flash.bat)

setlocal
cd /d "%~dp0\..\.."

where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: cmake not found in PATH. Run: powershell -ExecutionPolicy Bypass -File scripts\bringup.ps1
    exit /b 1
)
where arm-none-eabi-gcc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ARM GCC toolchain not found in PATH. Run: powershell -ExecutionPolicy Bypass -File scripts\bringup.ps1
    exit /b 1
)

echo ====================================
echo NUCLEO-F401RE Build (CMake, firmware-debug)
echo ====================================
cmake --preset firmware-debug
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build --preset firmware-debug
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

echo.
echo Build completed successfully. Output in build\ (StepperMotorController.elf/.hex/.bin/.map)

if "%1"=="flash" (
    echo Flashing to target...
    openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/StepperMotorController.elf verify reset exit"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Flashing failed
        exit /b 1
    )
    echo Flash completed successfully.
)

endlocal
exit /b 0
