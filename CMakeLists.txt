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
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/server/foundation/F_platform/STM32F401RETx_FLASH.ld")
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -specs=nano.specs -T${LINKER_SCRIPT} -Wl,-Map=${PROJECT_NAME}.map,--cref -Wl,--gc-sections -lc -lm -lnosys")

# C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    server
    server/layers
    server/layers/L1_transport
    server/layers/L2_protocol
    server/layers/L3_services
    server/layers/L4_drivers
    server/layers/L5_board
    server/foundation
    server/foundation/F_platform
    server/external/X_vendor/CMSIS
    server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/include
    server/external/X_middlewares/Third_Party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
    # SEGGER RTT and SystemView
    server/external/X_middlewares/SEGGER/RTT
    server/external/X_middlewares/SEGGER/SystemView
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
set(FREERTOS_DIR server/external/X_middlewares/Third_Party/FreeRTOS-Kernel)
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
    server/external/X_middlewares/SEGGER/RTT/SEGGER_RTT.c
    server/external/X_middlewares/SEGGER/RTT/SEGGER_RTT_printf.c
)

# SEGGER SystemView sources
set(SEGGER_SYSVIEW_SOURCES
    server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.c
    server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_Config_FreeRTOS.c
    server/external/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_FreeRTOS.c
)

# Source files
set(C_SOURCES
    server/foundation/F_platform/system_stm32f4xx.c
    server/foundation/F_platform/syscalls.c
    ${FREERTOS_SOURCES}
    ${SEGGER_RTT_SOURCES}
    ${SEGGER_SYSVIEW_SOURCES}
)

# Transport module sources (byte-level I/O)
set(TRANSPORT_SOURCES
    server/layers/L1_transport/uart_transport.cpp
    server/layers/L1_transport/rtt_transport.cpp
)

# Protocol module sources (command framing/dispatch)
set(PROTOCOL_SOURCES
    server/layers/L2_protocol/command_parser.cpp
    server/layers/L2_protocol/telemetry.cpp
    server/layers/L2_protocol/event_codec.cpp
)

# Task sources
set(TASK_SOURCES
    server/foundation/F_platform/tasks/motor_task.cpp
    server/foundation/F_platform/tasks/encoder_task.cpp
    server/foundation/F_platform/tasks/display_task.cpp
    server/foundation/F_platform/tasks/comms_task.cpp
    server/foundation/F_platform/tasks/bringup_task.cpp
)

# Service sources — motion
set(SERVICE_MOTION_SOURCES
    server/layers/L3_services/motion/motion_service.cpp
    server/layers/L3_services/motion/motor_config.cpp
    server/layers/L3_services/motion/control_mode.cpp
    server/layers/L3_services/motion/following_supervisor.cpp
    server/layers/L3_services/motion/pid_controller.cpp
    server/layers/L3_services/motion/speed_trim_controller.cpp
    server/layers/L3_services/motion/sysid.cpp
)

# Service sources — safety
set(SERVICE_SAFETY_SOURCES
    server/layers/L3_services/safety/safety_service.cpp
)

# Service sources — config
set(SERVICE_CONFIG_SOURCES
    server/layers/L3_services/config/config_service.cpp
    server/layers/L3_services/config/device_config.cpp
)

# Service sources — dispatch
set(SERVICE_DISPATCH_SOURCES
    server/layers/L3_services/dispatch/command_queue.cpp
    server/layers/L3_services/dispatch/event_service.cpp
)

# Service sources — infra
set(SERVICE_INFRA_SOURCES
    server/layers/L3_services/infra/tick_timer.cpp
    server/layers/L3_services/infra/timing_service.cpp
    server/layers/L3_services/infra/trace.cpp
    server/layers/L3_services/infra/unit_conversion.cpp
    server/layers/L3_services/infra/indicator_service.cpp
    server/layers/L3_services/infra/flash_image_service.cpp
)

# UI sources
set(UI_SOURCES
    server/layers/L3_services/ui/ui_mode.cpp
    server/layers/L3_services/ui/menu_screen.cpp
    server/layers/L3_services/ui/terminal_screen.cpp
    server/layers/L3_services/ui/screen_manager.cpp
    server/layers/L3_services/ui/screens/boot_color_screen.cpp
    server/layers/L3_services/ui/screens/device_info_screen.cpp
    server/layers/L3_services/ui/screens/encoder_screen.cpp
    server/layers/L3_services/ui/screens/motion_screen.cpp
    server/layers/L3_services/ui/screens/config_screen.cpp
    server/layers/L3_services/ui/screens/graph_screen.cpp
    server/layers/L3_services/ui/screens/image_view_screen.cpp
    server/layers/L3_services/ui/screens/trace_screen.cpp
)

# Driver sources
set(DRIVER_SOURCES
    server/layers/L4_drivers/spi/spi_manager.cpp
)

set(CXX_SOURCES
    server/main.cpp
    ${TRANSPORT_SOURCES}
    ${PROTOCOL_SOURCES}
    ${TASK_SOURCES}
    ${SERVICE_MOTION_SOURCES}
    ${SERVICE_SAFETY_SOURCES}
    ${SERVICE_CONFIG_SOURCES}
    ${SERVICE_DISPATCH_SOURCES}
    ${SERVICE_INFRA_SOURCES}
    ${UI_SOURCES}
    ${DRIVER_SOURCES}
)

set(ASM_SOURCES
    server/foundation/F_platform/startup_stm32f401xe.s
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
