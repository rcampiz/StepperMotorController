/**
 * @file itrace_context.hpp
 * @brief Trace context identifiers and setters — boundary contract
 *
 * Task and service IDs for the trace system. Scheduler tasks set
 * context at the top of their main loop; L3 Trace records it.
 */

#pragma once

#include <stdint.h>

namespace Harness {

// Service context identifiers (set before traced calls)
enum ServiceId : uint8_t {
    SVC_NONE     = 0,
    SVC_MOTION   = 1,
    SVC_CONFIG   = 2,
    SVC_SAFETY   = 3,
    SVC_ENCODER  = 4,
    SVC_SYSID    = 5,
    SVC_COMMS    = 6,
    SVC_UI       = 7,
    SVC_DISPATCH = 8,
    SVC_COUNT    = 9,
};

// Task identifiers (set at top of each RTOS task's main loop)
enum TaskId : uint8_t {
    TASK_UNKNOWN  = 0,
    TASK_MOTOR    = 1,
    TASK_ENCODER  = 2,
    TASK_COMMS    = 3,
    TASK_DISPLAY  = 4,
    TASK_TIMER    = 5,
    TASK_COUNT    = 6,
};

/**
 * @brief Set the current task context for subsequent trace entries
 * Call at the top of each RTOS task's main loop.
 */
void setTraceTaskId(uint8_t id);

/**
 * @brief Set the current service context for subsequent trace entries
 */
void setTraceServiceId(uint8_t id);

} // namespace Harness
