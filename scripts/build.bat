@echo off
REM Build script for NUCLEO-F401RE project using CMake + NMake

setlocal enabledelayedexpansion

REM Change to project root directory
cd /d "%~dp0\.."

REM Parse command line argument
set BUILD_TARGET=%1
if "%BUILD_TARGET%"=="" set BUILD_TARGET=all

echo ====================================
echo NUCLEO-F401RE Build Script (CMake)
echo ====================================
echo.

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found in PATH
    exit /b 1
)

REM Check if ARM GCC is available
where arm-none-eabi-gcc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ARM GCC toolchain not found in PATH
    exit /b 1
)

REM Check if NMake is available
where nmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo WARNING: NMake not found, falling back to direct build...
    echo.
    call "%~dp0\build_direct.bat" %BUILD_TARGET%
    exit /b !ERRORLEVEL!
)

echo Using CMake + NMake build system
echo.

REM Create build directory
if not exist "build" mkdir build

REM Handle clean target
if "%BUILD_TARGET%"=="clean" (
    echo Cleaning build directory...
    rmdir /S /Q build 2>nul
    mkdir build
    echo Build directory cleaned.
    exit /b 0
)

REM Configure with CMake
echo [1/3] Configuring with CMake...
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    exit /b 1
)

REM Build the project
echo [2/3] Building with NMake...
if "%BUILD_TARGET%"=="flash" (
    cmake --build build
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo [3/3] Flashing to target...
        cmake --build build --target flash
    )
) else (
    cmake --build build
)

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ====================================
    echo Build completed successfully!
    echo ====================================
    exit /b 0
) else (
    echo.
    echo ====================================
    echo Build FAILED!
    echo ====================================
    exit /b 1
)

endlocal