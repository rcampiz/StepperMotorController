cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# StepperMotorController firmware (NUCLEO-F401RE)
#
# Authoritative build. Configure with a preset:
#   cmake --preset firmware-debug && cmake --build --preset firmware-debug
# Output lands in build/ (StepperMotorController.elf/.hex/.bin/.map) so
# scripts/flash.bat and .vscode/launch.json keep working.
#
# Host-side unit tests are a separate project: see tests/CMakeLists.txt
# (preset "host-tests").
# ---------------------------------------------------------------------------
project(StepperMotorController C CXX ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Interface-boundary tracing (taps + RTT emitter). Debug-only: the firmware-debug
# preset turns it ON, firmware-release leaves it OFF.
option(ENABLE_INTERFACE_TRACE "Compile harness taps and interface trace output" OFF)

# ---------------------------------------------------------------------------
# MCU flags (identical to scripts/build_direct.bat)
# ---------------------------------------------------------------------------
set(MCU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

add_compile_options(
    ${MCU_FLAGS}
    -Wall -fdata-sections -ffunction-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:-x>
    $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
)
set(CMAKE_C_FLAGS_DEBUG   "-Og -g -gdwarf-2")
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g -gdwarf-2")
set(CMAKE_ASM_FLAGS_DEBUG "-g -gdwarf-2")
set(CMAKE_C_FLAGS_RELEASE   "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/server/foundation/F_platform/startup/STM32F401RETx_FLASH.ld)
add_link_options(
    ${MCU_FLAGS}
    -specs=nano.specs
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map,--cref
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
)

# ---------------------------------------------------------------------------
# Include paths and defines (identical to scripts/build_direct.bat)
# ---------------------------------------------------------------------------
set(EXT server/external)
set(FREERTOS_DIR ${EXT}/X_middlewares/Third_Party/FreeRTOS-Kernel)

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
    server/foundation/F_platform/rtos
    ${EXT}
    ${EXT}/X_vendor/CMSIS
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F
    ${EXT}/X_middlewares/SEGGER/RTT
    ${EXT}/X_middlewares/SEGGER/SystemView
)

add_compile_definitions(
    STM32F401xE
    __FPU_PRESENT=1
    __FPU_USED=1
    ENABLE_SEGGER_SYSTEMVIEW
    RTT_USE_ASM=0
    $<$<BOOL:${ENABLE_INTERFACE_TRACE}>:ENABLE_INTERFACE_TRACE>
)

# ---------------------------------------------------------------------------
# Sources
#   Third-party: explicit list.
#   Application: every .cpp/.c under server/{layers,foundation,harness} plus
#   server/*.cpp. The layer directories ARE the module boundaries, so the
#   directory tree is the source list; CONFIGURE_DEPENDS re-globs on build.
# ---------------------------------------------------------------------------
set(THIRD_PARTY_SOURCES
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
    ${EXT}/X_middlewares/SEGGER/RTT/SEGGER_RTT.c
    ${EXT}/X_middlewares/SEGGER/RTT/SEGGER_RTT_printf.c
    ${EXT}/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.c
    ${EXT}/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_Config_FreeRTOS.c
    ${EXT}/X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW_FreeRTOS.c
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/server/layers/*.cpp
    ${CMAKE_SOURCE_DIR}/server/foundation/*.cpp
    ${CMAKE_SOURCE_DIR}/server/foundation/*.c
    ${CMAKE_SOURCE_DIR}/server/harness/*.cpp
)
file(GLOB ROOT_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/server/*.cpp)

set(ASM_SOURCES server/foundation/F_platform/startup/startup_stm32f401xe.s)
set_source_files_properties(${ASM_SOURCES} PROPERTIES LANGUAGE ASM)

add_executable(${PROJECT_NAME}.elf
    ${THIRD_PARTY_SOURCES}
    ${APP_SOURCES}
    ${ROOT_SOURCES}
    ${ASM_SOURCES}
)
target_link_libraries(${PROJECT_NAME}.elf c m nosys)

# Match build_direct.bat output names/locations
set_target_properties(${PROJECT_NAME}.elf PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Producing ${PROJECT_NAME}.hex / .bin and size report"
)

add_custom_target(flash
    COMMAND openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
            -c "program $<TARGET_FILE:${PROJECT_NAME}.elf> verify reset exit"
    DEPENDS ${PROJECT_NAME}.elf
    COMMENT "Flashing ${PROJECT_NAME}.elf via ST-LINK"
)

# Architecture checkers as a build-time target (python required)
add_custom_target(arch-check
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/check_includes.py --strict
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/check_harness_defs.py --strict
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/gen_architecture.py --check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking layer/harness include rules"
)

message(STATUS "Build type:              ${CMAKE_BUILD_TYPE}")
message(STATUS "ENABLE_INTERFACE_TRACE:  ${ENABLE_INTERFACE_TRACE}")
list(LENGTH APP_SOURCES _n_app)
message(STATUS "Application sources:     ${_n_app}")
