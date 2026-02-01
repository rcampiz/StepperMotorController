@echo off
REM Direct build script for NUCLEO-F401RE (no make/nmake required)

setlocal enabledelayedexpansion

REM Change to project root directory
cd /d "%~dp0\.."

echo ====================================
echo NUCLEO-F401RE Direct Build Script
echo ====================================
echo.

REM Check if ARM GCC is available
where arm-none-eabi-gcc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ARM GCC toolchain not found in PATH
    exit /b 1
)

REM Create build directory
if not exist "build" mkdir build

REM Compiler flags
set "MCU_FLAGS=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
set "FREERTOS_INC=-IMiddlewares/Third_Party/FreeRTOS-Kernel/include -IMiddlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F"
set "INCLUDES=-ICore/Inc -IDrivers/CMSIS %FREERTOS_INC%"
set "DEFINES=-DSTM32F401xE"
set "CFLAGS=%MCU_FLAGS% %INCLUDES% %DEFINES% -Og -Wall -fdata-sections -ffunction-sections -g -gdwarf-2"
set "CXXFLAGS=%CFLAGS% -std=c++14 -fno-exceptions -fno-rtti -fno-use-cxa-atexit"
set "ASFLAGS=%MCU_FLAGS% -x assembler-with-cpp"
set "LDFLAGS=%MCU_FLAGS% -specs=nano.specs -TSTM32F401RETx_FLASH.ld -Wl,-Map=build/LedArray.map,--cref -Wl,--gc-sections -Wl,--no-warn-rwx-segments -lc -lm -lnosys"

echo [1/18] Compiling system_stm32f4xx.c...
arm-none-eabi-gcc -c %CFLAGS% Core/Src/system_stm32f4xx.c -o build/system_stm32f4xx.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile system_stm32f4xx.c & exit /b 1)

echo [2/18] Compiling syscalls.c...
arm-none-eabi-gcc -c %CFLAGS% Core/Src/syscalls.c -o build/syscalls.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile syscalls.c & exit /b 1)

echo [3/18] Compiling FreeRTOS tasks.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/tasks.c -o build/tasks.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile tasks.c & exit /b 1)

echo [4/18] Compiling FreeRTOS queue.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/queue.c -o build/queue.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile queue.c & exit /b 1)

echo [5/18] Compiling FreeRTOS list.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/list.c -o build/list.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile list.c & exit /b 1)

echo [6/18] Compiling FreeRTOS timers.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/timers.c -o build/timers.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile timers.c & exit /b 1)

echo [7/18] Compiling FreeRTOS event_groups.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/event_groups.c -o build/event_groups.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile event_groups.c & exit /b 1)

echo [8/18] Compiling FreeRTOS stream_buffer.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/stream_buffer.c -o build/stream_buffer.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile stream_buffer.c & exit /b 1)

echo [9/18] Compiling FreeRTOS heap_4.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/portable/MemMang/heap_4.c -o build/heap_4.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile heap_4.c & exit /b 1)

echo [10/18] Compiling FreeRTOS port.c...
arm-none-eabi-gcc -c %CFLAGS% Middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c -o build/port.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile port.c & exit /b 1)

echo [11/18] Compiling main.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% Core/Src/main.cpp -o build/main.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile main.cpp & exit /b 1)

echo [12/18] Compiling led_pattern_task.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% Core/Src/led_pattern_task.cpp -o build/led_pattern_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile led_pattern_task.cpp & exit /b 1)

echo [13/18] Compiling heartbeat_task.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% Core/Src/heartbeat_task.cpp -o build/heartbeat_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile heartbeat_task.cpp & exit /b 1)

echo [14/18] Compiling button_monitor_task.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% Core/Src/button_monitor_task.cpp -o build/button_monitor_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile button_monitor_task.cpp & exit /b 1)

echo [15/18] Compiling usb_comm_task.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% Core/Src/usb_comm_task.cpp -o build/usb_comm_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile usb_comm_task.cpp & exit /b 1)

echo [16/18] Assembling startup_stm32f401xe.s...
arm-none-eabi-gcc -c %ASFLAGS% Core/Src/startup_stm32f401xe.s -o build/startup_stm32f401xe.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to assemble startup_stm32f401xe.s & exit /b 1)

echo [17/18] Collecting object files...
set "OBJS=build/system_stm32f4xx.o build/syscalls.o build/tasks.o build/queue.o build/list.o"
set "OBJS=%OBJS% build/timers.o build/event_groups.o build/stream_buffer.o build/heap_4.o build/port.o"
set "OBJS=%OBJS% build/main.o build/led_pattern_task.o build/heartbeat_task.o build/button_monitor_task.o"
set "OBJS=%OBJS% build/usb_comm_task.o build/startup_stm32f401xe.o"

echo [18/18] Linking...
arm-none-eabi-g++ %OBJS% %LDFLAGS% -o build/LedArray.elf
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Linking failed
    exit /b 1
)

echo.
echo Creating HEX file...
arm-none-eabi-objcopy -O ihex build/LedArray.elf build/LedArray.hex

echo Creating BIN file...
arm-none-eabi-objcopy -O binary -S build/LedArray.elf build/LedArray.bin

echo.
echo Firmware size:
arm-none-eabi-size build/LedArray.elf

echo.
echo ====================================
echo Build completed successfully!
echo ====================================
echo.
echo Output files in build/:
echo   - LedArray.elf  (ELF executable)
echo   - LedArray.hex  (Intel HEX format)
echo   - LedArray.bin  (Binary format)
echo   - LedArray.map  (Memory map)
echo.

if "%1"=="flash" (
    echo Flashing to target...
    openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/LedArray.elf verify reset exit"
)

endlocal