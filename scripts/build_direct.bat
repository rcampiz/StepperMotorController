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
set "FREERTOS_INC=-Iserver/external/X_middlewares/Third_Party/FreeRTOS-Kernel/include -Iserver/external/X_middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F"
set "INCLUDES=-Iserver -Iserver/layers -Iserver/layers/L1_transport -Iserver/layers/L2_protocol -Iserver/layers/L3_services -Iserver/layers/L4_drivers -Iserver/layers/L5_board -Iserver/foundation -Iserver/foundation/F_platform -Iserver/foundation/F_platform/rtos -Iserver/wiring -Iserver/external -Iserver/external/X_vendor/CMSIS %FREERTOS_INC% -Iserver/external/X_middlewares/SEGGER/RTT -Iserver/external/X_middlewares/SEGGER/SystemView"
set "DEFINES=-DSTM32F401xE -D__FPU_PRESENT=1 -D__FPU_USED=1 -DENABLE_SEGGER_SYSTEMVIEW -DRTT_USE_ASM=0 -DENABLE_INTERFACE_TRACE"
set "CFLAGS=%MCU_FLAGS% %INCLUDES% %DEFINES% -Og -Wall -fdata-sections -ffunction-sections -g -gdwarf-2"
set "CXXFLAGS=%CFLAGS% -std=c++14 -fno-exceptions -fno-rtti -fno-use-cxa-atexit"
set "ASFLAGS=%MCU_FLAGS% -x assembler-with-cpp"
set "LDFLAGS=%MCU_FLAGS% -specs=nano.specs -Tserver/foundation/F_platform/startup/STM32F401RETx_FLASH.ld -Wl,-Map=build/StepperMotorController.map,--cref -Wl,--gc-sections -Wl,--no-warn-rwx-segments -lc -lm -lnosys"

echo [1/24] Compiling system_stm32f4xx.c...
arm-none-eabi-gcc -c %CFLAGS% server/foundation/F_platform/startup/system_stm32f4xx.c -o build/system_stm32f4xx.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile system_stm32f4xx.c & exit /b 1)

echo [2/24] Compiling syscalls.c...
arm-none-eabi-gcc -c %CFLAGS% server/foundation/F_platform/startup/syscalls.c -o build/syscalls.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile syscalls.c & exit /b 1)

echo [3/24] Compiling FreeRTOS tasks.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/tasks.c -o build/tasks.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile tasks.c & exit /b 1)

echo [4/24] Compiling FreeRTOS queue.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/queue.c -o build/queue.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile queue.c & exit /b 1)

echo [5/24] Compiling FreeRTOS list.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/list.c -o build/list.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile list.c & exit /b 1)

echo [6/24] Compiling FreeRTOS timers.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/timers.c -o build/timers.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile timers.c & exit /b 1)

echo [7/24] Compiling FreeRTOS event_groups.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/event_groups.c -o build/event_groups.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile event_groups.c & exit /b 1)

echo [8/24] Compiling FreeRTOS stream_buffer.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/stream_buffer.c -o build/stream_buffer.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile stream_buffer.c & exit /b 1)

echo [9/24] Compiling FreeRTOS heap_4.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/portable/MemMang/heap_4.c -o build/heap_4.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile heap_4.c & exit /b 1)

echo [10/24] Compiling FreeRTOS port.c...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c -o build/port.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile port.c & exit /b 1)

echo [11/24] Compiling SEGGER RTT...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/SEGGER/RTT/SEGGER_RTT.c -o build/SEGGER_RTT.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile SEGGER_RTT.c & exit /b 1)
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/SEGGER/RTT/SEGGER_RTT_printf.c -o build/SEGGER_RTT_printf.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile SEGGER_RTT_printf.c & exit /b 1)

echo [12/24] Compiling SEGGER SystemView...
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.c -o build/SEGGER_SYSVIEW.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile SEGGER_SYSVIEW.c & exit /b 1)
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_Config_FreeRTOS.c -o build/SEGGER_SYSVIEW_Config_FreeRTOS.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile SEGGER_SYSVIEW_Config_FreeRTOS.c & exit /b 1)
arm-none-eabi-gcc -c %CFLAGS% server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_FreeRTOS.c -o build/SEGGER_SYSVIEW_FreeRTOS.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile SEGGER_SYSVIEW_FreeRTOS.c & exit /b 1)

echo [13/24] Compiling main.cpp...
arm-none-eabi-g++ -c %CXXFLAGS% server/main.cpp -o build/main.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile main.cpp & exit /b 1)

echo [14/24] Compiling transport sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L1_transport/uart_transport.cpp -o build/uart_transport.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile uart_transport.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L1_transport/rtt_transport.cpp -o build/rtt_transport.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile rtt_transport.cpp & exit /b 1)

echo [15/24] Compiling protocol sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L2_protocol/command_parser.cpp -o build/command_parser.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile command_parser.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/interfaces/telemetry.cpp -o build/telemetry.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile telemetry.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L2_protocol/event_codec.cpp -o build/event_codec.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile event_codec.cpp & exit /b 1)

echo [16/24] Compiling task sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/tasks/motor_task.cpp -o build/motor_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile motor_task.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/tasks/encoder_task.cpp -o build/encoder_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile encoder_task.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/tasks/display_task.cpp -o build/display_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile display_task.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/tasks/comms_task.cpp -o build/comms_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile comms_task.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/tasks/bringup_task.cpp -o build/bringup_task.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile bringup_task.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/system_init.cpp -o build/system_init.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile system_init.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/rtos/platform_init.cpp -o build/platform_init.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile platform_init.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/rtos/freertos_hooks.cpp -o build/freertos_hooks.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile freertos_hooks.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/rtos/runtime_stats.cpp -o build/runtime_stats.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile runtime_stats.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/rtos/freertos_task_stats.cpp -o build/freertos_task_stats.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile freertos_task_stats.cpp & exit /b 1)

echo [17/25] Compiling service sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/motion_service.cpp -o build/motion_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile motion_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/motor_config.cpp -o build/motor_config.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile motor_config.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/control_mode.cpp -o build/control_mode.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile control_mode.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/following_supervisor.cpp -o build/following_supervisor.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile following_supervisor.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/pid_controller.cpp -o build/pid_controller.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile pid_controller.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/speed_trim_controller.cpp -o build/speed_trim_controller.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile speed_trim_controller.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/motion/sysid.cpp -o build/sysid.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile sysid.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/safety/safety_service.cpp -o build/safety_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile safety_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/config/config_service.cpp -o build/config_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile config_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/config/device_config.cpp -o build/device_config.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile device_config.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/dispatch/command_queue.cpp -o build/command_queue.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile command_queue.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/dispatch/event_service.cpp -o build/event_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile event_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/tick_timer.cpp -o build/tick_timer.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile tick_timer.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/timing_service.cpp -o build/timing_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile timing_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/trace.cpp -o build/trace.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile trace.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/unit_conversion.cpp -o build/unit_conversion.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile unit_conversion.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/indicator_service.cpp -o build/indicator_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile indicator_service.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/infra/flash_image_service.cpp -o build/flash_image_service.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile flash_image_service.cpp & exit /b 1)

echo [18/25] Compiling UI sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/ui_mode.cpp -o build/ui_mode.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile ui_mode.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/menu_screen.cpp -o build/menu_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile menu_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/terminal_screen.cpp -o build/terminal_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile terminal_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screen_manager.cpp -o build/screen_manager.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile screen_manager.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/boot_color_screen.cpp -o build/boot_color_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile boot_color_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/device_info_screen.cpp -o build/device_info_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile device_info_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/encoder_screen.cpp -o build/encoder_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile encoder_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/motion_screen.cpp -o build/motion_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile motion_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/config_screen.cpp -o build/config_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile config_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/graph_screen.cpp -o build/graph_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile graph_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/image_view_screen.cpp -o build/image_view_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile image_view_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/trace_screen.cpp -o build/trace_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile trace_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/task_monitor_screen.cpp -o build/task_monitor_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile task_monitor_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/dispatcher_screen.cpp -o build/dispatcher_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile dispatcher_screen.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L3_services/ui/screens/arch_screen.cpp -o build/arch_screen.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile arch_screen.cpp & exit /b 1)

echo [19/26] Compiling driver sources...
arm-none-eabi-g++ -c %CXXFLAGS% server/layers/L4_drivers/spi/spi_manager.cpp -o build/spi_manager.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile spi_manager.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_util/interface_trace.cpp -o build/interface_trace.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile interface_trace.cpp & exit /b 1)
arm-none-eabi-g++ -c %CXXFLAGS% server/foundation/F_platform/interfaces/async_event_types.cpp -o build/async_event_types.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to compile async_event_types.cpp & exit /b 1)

echo [20/27] Assembling startup_stm32f401xe.s...
arm-none-eabi-gcc -c %ASFLAGS% server/foundation/F_platform/startup/startup_stm32f401xe.s -o build/startup_stm32f401xe.o
if %ERRORLEVEL% NEQ 0 (echo ERROR: Failed to assemble startup_stm32f401xe.s & exit /b 1)

echo [21/26] Collecting object files...
set "OBJS=build/system_stm32f4xx.o build/syscalls.o build/tasks.o build/queue.o build/list.o"
set "OBJS=%OBJS% build/timers.o build/event_groups.o build/stream_buffer.o build/heap_4.o build/port.o"
set "OBJS=%OBJS% build/SEGGER_RTT.o build/SEGGER_RTT_printf.o build/SEGGER_SYSVIEW.o build/SEGGER_SYSVIEW_Config_FreeRTOS.o build/SEGGER_SYSVIEW_FreeRTOS.o"
set "OBJS=%OBJS% build/main.o build/uart_transport.o build/rtt_transport.o build/command_parser.o build/telemetry.o build/event_codec.o"
set "OBJS=%OBJS% build/motor_task.o build/encoder_task.o build/display_task.o build/comms_task.o build/bringup_task.o build/system_init.o build/platform_init.o build/freertos_hooks.o build/runtime_stats.o build/freertos_task_stats.o"
set "OBJS=%OBJS% build/tick_timer.o build/command_queue.o build/device_config.o build/control_mode.o build/motor_config.o build/motion_service.o build/safety_service.o build/config_service.o build/trace.o build/event_service.o build/flash_image_service.o build/indicator_service.o build/unit_conversion.o build/timing_service.o build/pid_controller.o build/following_supervisor.o build/sysid.o build/speed_trim_controller.o"
set "OBJS=%OBJS% build/ui_mode.o build/menu_screen.o build/terminal_screen.o build/screen_manager.o"
set "OBJS=%OBJS% build/boot_color_screen.o build/device_info_screen.o build/encoder_screen.o build/motion_screen.o build/config_screen.o build/graph_screen.o build/image_view_screen.o build/trace_screen.o build/task_monitor_screen.o build/dispatcher_screen.o build/arch_screen.o"
set "OBJS=%OBJS% build/spi_manager.o build/interface_trace.o build/async_event_types.o"
set "OBJS=%OBJS% build/startup_stm32f401xe.o"

echo [22/26] Linking...
arm-none-eabi-g++ %OBJS% %LDFLAGS% -o build/StepperMotorController.elf
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Linking failed
    exit /b 1
)

echo.
echo Creating HEX file...
arm-none-eabi-objcopy -O ihex build/StepperMotorController.elf build/StepperMotorController.hex

echo Creating BIN file...
arm-none-eabi-objcopy -O binary -S build/StepperMotorController.elf build/StepperMotorController.bin

echo.
echo Firmware size:
arm-none-eabi-size build/StepperMotorController.elf

echo.
echo ====================================
echo Build completed successfully!
echo ====================================
echo.
echo Output files in build/:
echo   - StepperMotorController.elf  (ELF executable)
echo   - StepperMotorController.hex  (Intel HEX format)
echo   - StepperMotorController.bin  (Binary format)
echo   - StepperMotorController.map  (Memory map)
echo.

if "%1"=="flash" (
    echo Flashing to target...
    openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/StepperMotorController.elf verify reset exit"
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: Flashing failed
        exit /b 1
    )
    echo Flash completed successfully.
)

endlocal
exit /b 0
