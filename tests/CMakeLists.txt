cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Host-side unit tests for the firmware's RTOS-free layers.
#
# Standalone project built with the native compiler (WinLibs g++ on Windows):
#   cmake --preset host-tests -S tests   (from repo root)   OR
#   cmake -S tests -B build-tests -G Ninja && cmake --build build-tests && ctest --test-dir build-tests
#
# Each test executable links the production sources it exercises plus fakes
# that stand in for the other side of a harness. No FreeRTOS, no CMSIS.
# ---------------------------------------------------------------------------
project(smc_host_tests CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

set(SERVER ${CMAKE_CURRENT_SOURCE_DIR}/../server)

add_library(smc_test_env INTERFACE)
target_include_directories(smc_test_env INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}           # minitest.hpp, fakes/
    ${SERVER}
    ${SERVER}/layers
    ${SERVER}/foundation
)
target_compile_options(smc_test_env INTERFACE -Wall -Wextra -g -O0)
# Host builds never trace; taps are not compiled.
target_compile_definitions(smc_test_env INTERFACE SMC_HOST_TEST=1)

# smc_add_test(<name> <test source> [production sources...])
function(smc_add_test name src)
    add_executable(${name} ${src} ${ARGN})
    target_link_libraries(${name} PRIVATE smc_test_env)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# --- Step 0: boundary headers compile on the host, and the test runner works.
smc_add_test(test_smoke test_smoke.cpp)

# --- Step 1: motor telemetry is written by the service, not by a trace tap.
smc_add_test(test_telemetry_store test_telemetry_store.cpp
    ${SERVER}/layers/L3_services/infra/telemetry_store/telemetry_store.cpp)

# --- Step 3: H_L4_L5 — L4 drivers run on fake Board:: pins.
smc_add_test(test_powerstep01 test_powerstep01.cpp)
smc_add_test(test_lcd_vsync   test_lcd_vsync.cpp)

# --- Step 4a: H_L3_L4 — L3 config manager persists through a fake Drivers::IConfigStore.
smc_add_test(test_motor_config test_motor_config.cpp
    ${SERVER}/layers/L3_services/motion/motor_config/motor_config.cpp)

# --- Step 5: H_L1_L2 — the L2 parser formats every host-facing byte over a fake transport.
file(GLOB L2_HANDLER_SOURCES ${SERVER}/layers/L2_protocol/handlers/*.cpp)
smc_add_test(test_command_parser test_command_parser.cpp
    ${SERVER}/layers/L2_protocol/command_parser.cpp
    ${SERVER}/layers/L2_protocol/service_dispatcher.cpp
    ${SERVER}/layers/L2_protocol/event_codec.cpp
    ${L2_HANDLER_SOURCES}
    ${SERVER}/harness/pins/async_event.cpp)

# --- Step 6: H_L2_L3 — DBG:UNLOCK gate, task suspend via Platform, raw commands via L4.
smc_add_test(test_debug_service test_debug_service.cpp
    ${SERVER}/layers/L3_services/debug/debug_service.cpp
    ${SERVER}/harness/defs/task_handles.cpp)

# --- Step 4b: H_TASKS_L3 — the encoder sampling logic runs in L3 against fake hardware.
smc_add_test(test_encoder_service test_encoder_service.cpp
    ${SERVER}/layers/L3_services/motion/encoder_service/encoder_service.cpp
    ${SERVER}/layers/L3_services/infra/telemetry_store/telemetry_store.cpp)

# --- Step 7: H_TASKS_L3 — Comms task body in L3; bounded ARM/START flush; ESTOP bypass.
smc_add_test(test_comms_service test_comms_service.cpp
    ${SERVER}/layers/L3_services/comms/comms_service/comms_service.cpp
    ${SERVER}/layers/L3_services/infra/telemetry_store/telemetry_store.cpp
    ${SERVER}/layers/L3_services/ui/ui_mode/ui_mode.cpp)
smc_add_test(test_command_queue test_command_queue.cpp
    ${SERVER}/layers/L3_services/dispatch/command_queue/command_queue.cpp
    ${SERVER}/layers/L3_services/infra/tick_timer/tick_timer.cpp)
