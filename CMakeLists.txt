cmake_minimum_required(VERSION 3.20)

# Project settings
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# Toolchain settings
set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

# Don't try to run the compiler during configuration
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Project name
project(StepperMotorController C CXX ASM)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MCU settings
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# Compiler flags
set(CMAKE_C_FLAGS "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti -fno-use-cxa-atexit")
set(CMAKE_ASM_FLAGS "${MCU_FLAGS} -x assembler-with-cpp")

# Debug/Release flags
set(CMAKE_C_FLAGS_DEBUG "-Og -g -gdwarf-2")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g -gdwarf-2")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Linker flags
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32F401RETx_FLASH.ld")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nano.specs -T${LINKER_SCRIPT} -Wl,-Map=${PROJECT_NAME}.map,--cref -Wl,--gc-sections -lc -lm -lnosys")

# C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    Core/Inc
    Core/Inc/board
    Core/Inc/comms
    Core/Inc/drivers
    Core/Inc/tasks
    Core/Inc/services
    Drivers/CMSIS
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Drivers/STM32F4xx_HAL_Driver/Inc
    Middlewares/Third_Party/FreeRTOS-Kernel/include
    Middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
    # SEGGER RTT and SystemView
    Middlewares/SEGGER/RTT
    Middlewares/SEGGER/SystemView
)

# Defines
add_compile_definitions(
    STM32F401xE
    __FPU_PRESENT=1
    __FPU_USED=1
    ENABLE_SEGGER_SYSTEMVIEW
    RTT_USE_ASM=0
)

# FreeRTOS source files
set(FREERTOS_DIR Middlewares/Third_Party/FreeRTOS-Kernel)
set(FREERTOS_SOURCES
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
)

# SEGGER RTT sources
set(SEGGER_RTT_SOURCES
    Middlewares/SEGGER/RTT/SEGGER_RTT.c
    Middlewares/SEGGER/RTT/SEGGER_RTT_printf.c
)

# SEGGER SystemView sources
set(SEGGER_SYSVIEW_SOURCES
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.c
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_Config_FreeRTOS.c
    Middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_FreeRTOS.c
)

# Source files
set(C_SOURCES
    Core/Src/system_stm32f4xx.c
    Core/Src/syscalls.c
    ${FREERTOS_SOURCES}
    ${SEGGER_RTT_SOURCES}
    ${SEGGER_SYSVIEW_SOURCES}
)

# Communications module sources
set(COMMS_SOURCES
    Core/Src/comms/uart_transport.cpp
    Core/Src/comms/rtt_transport.cpp
    Core/Src/comms/command_parser.cpp
    Core/Src/comms/telemetry.cpp
)

# Task sources
set(TASK_SOURCES
    Core/Src/tasks/motor_task.cpp
    Core/Src/tasks/encoder_task.cpp
    Core/Src/tasks/display_task.cpp
    Core/Src/tasks/comms_task.cpp
    Core/Src/tasks/bringup_task.cpp
)

# Service sources
set(SERVICE_SOURCES
    Core/Src/services/tick_timer.cpp
    Core/Src/services/command_queue.cpp
    Core/Src/services/device_config.cpp
    Core/Src/services/control_mode.cpp
)

set(CXX_SOURCES
    Core/Src/main.cpp
    ${COMMS_SOURCES}
    ${TASK_SOURCES}
    ${SERVICE_SOURCES}
)

set(ASM_SOURCES
    Core/Src/startup_stm32f401xe.s
)

# Create executable
add_executable(${PROJECT_NAME}.elf
    ${C_SOURCES}
    ${CXX_SOURCES}
    ${ASM_SOURCES}
)

# Set ASM language for startup file
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM)

# Custom commands for hex and bin files
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Building ${PROJECT_NAME}.hex and ${PROJECT_NAME}.bin"
)

# Flash target
add_custom_target(flash
    COMMAND openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program ${PROJECT_NAME}.elf verify reset exit"
    DEPENDS ${PROJECT_NAME}.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Flashing ${PROJECT_NAME}.elf to target"
)

# Print build info
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")