cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Virtual Nucleo: the firmware's L2 protocol, L3 services and task bodies on a desktop,
# with fake hardware and a TCP "serial port". Native compiler, no FreeRTOS, no CMSIS.
#
#   cmake --preset vnucleo            (from sim/)
#   cmake --build --preset vnucleo
#   build/vnucleo --port 5761
#
# See sim/README.md for running one board or four.
# ---------------------------------------------------------------------------
project(smc_vnucleo CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

set(SERVER ${CMAKE_CURRENT_SOURCE_DIR}/../server)
set(L2 ${SERVER}/layers/L2_protocol)
set(L3 ${SERVER}/layers/L3_services)

file(GLOB L2_HANDLERS ${L2}/handlers/*.cpp)

# The production sources that run unchanged. Left out: the init cascades that reach real
# hardware (motor/encoder/comms/display *_init.cpp, comms_protocol_init.cpp), the LCD
# screens and display service, and harness_connect.cpp (sim_stubs.cpp has connectL2L3).
set(FIRMWARE_SOURCES
    ${L2}/command_parser.cpp
    ${L2}/service_dispatcher.cpp
    ${L2}/event_codec.cpp
    ${L2_HANDLERS}
    ${L3}/comms/comms_service/comms_service.cpp
    ${L3}/comms/system_dispatch/system_dispatch.cpp
    ${L3}/comms/trace_dispatch/trace_dispatch.cpp
    ${L3}/config/config_service/config_service.cpp
    ${L3}/infra/trace/trace.cpp
    ${L3}/config/device_config/device_config.cpp
    ${L3}/debug/debug_service.cpp
    ${L3}/dispatch/command_queue/command_queue.cpp
    ${L3}/dispatch/dispatch_init.cpp
    ${L3}/dispatch/queue_dispatch/queue_dispatch.cpp
    ${L3}/display/display_dispatch/display_dispatch.cpp
    ${L3}/infra/flash_dispatch/flash_dispatch.cpp
    ${L3}/infra/flash_image_service/flash_image_service.cpp
    ${L3}/infra/telemetry_store/telemetry_store.cpp
    ${L3}/infra/tick_timer/tick_timer.cpp
    ${L3}/infra/timing_service/timing_service.cpp
    ${L3}/infra/unit_conversion/unit_conversion.cpp
    ${L3}/motion/config_dispatch/config_dispatch.cpp
    ${L3}/motion/control_mode/control_mode.cpp
    ${L3}/motion/control_mode_dispatch/control_mode_dispatch.cpp
    ${L3}/motion/encoder_dispatch/encoder_dispatch.cpp
    ${L3}/motion/encoder_processor/encoder_processor.cpp
    ${L3}/motion/encoder_service/encoder_service.cpp
    ${L3}/motion/following_supervisor/following_supervisor.cpp
    ${L3}/motion/motion.cpp
    ${L3}/motion/motion_dispatch/motion_dispatch.cpp
    ${L3}/motion/motion_service/motion_service.cpp
    ${L3}/motion/motor_config/motor_config.cpp
    ${L3}/motion/motor_controller/motor_controller.cpp
    ${L3}/motion/motor_driver_dispatch/motor_driver_dispatch.cpp
    ${L3}/motion/motor_event_service/motor_event_service.cpp
    ${L3}/motion/motor_service/motor_service.cpp
    ${L3}/motion/pid_controller/pid_controller.cpp
    ${L3}/motion/speed_trim_controller/speed_trim_controller.cpp
    ${L3}/motion/supervisor_trim_dispatch/supervisor_trim_dispatch.cpp
    ${L3}/motion/sysid/sysid.cpp
    ${L3}/motion/system_service_init.cpp
    ${L3}/safety/safety_dispatch/safety_dispatch.cpp
    ${L3}/safety/safety_service/safety_service.cpp
    ${L3}/ui/ui_mode/ui_mode.cpp
    ${SERVER}/foundation/F_platform/tasks/motor_task.cpp
    ${SERVER}/foundation/F_platform/tasks/comms_task.cpp
    ${SERVER}/foundation/F_platform/tasks/encoder_task.cpp
    ${SERVER}/harness/pins/async_event.cpp
    ${SERVER}/harness/pins/imicrosecond_clock.cpp
    ${SERVER}/harness/pins/oob_estop.cpp
    ${SERVER}/harness/pins/build_id.cpp
    ${SERVER}/harness/pins/itask_stats.cpp
    ${SERVER}/harness/pins/itrace_context.cpp
    ${SERVER}/harness/trace/interface_trace.cpp
)

add_executable(vnucleo
    vnucleo/main.cpp
    vnucleo/tcp_transport.cpp
    vnucleo/fake_powerstep.cpp
    vnucleo/sim_stubs.cpp
    ${FIRMWARE_SOURCES}
)
target_include_directories(vnucleo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vnucleo/shadow     # host versions of target-only headers
    ${CMAKE_CURRENT_SOURCE_DIR}/vnucleo
    ${SERVER}
    ${SERVER}/layers
    ${SERVER}/foundation
)
# SMC_HOST_TEST: host builds never trace (the same switch the host tests use).
target_compile_definitions(vnucleo PRIVATE SMC_HOST_TEST=1 GIT_HASH="vnucleo")
target_compile_options(vnucleo PRIVATE -Wall -Wextra -O1 -g)
target_link_libraries(vnucleo PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(vnucleo PRIVATE ws2_32)
    target_link_options(vnucleo PRIVATE -static)     # runs without the MinGW DLLs on PATH
endif()
