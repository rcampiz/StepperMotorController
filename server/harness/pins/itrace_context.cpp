/**
 * @file itrace_context.cpp
 * @brief Trace context storage — owns task/service ID statics
 *
 * Single-core safe: each RTOS task sets context at loop top,
 * trace record() reads it. No L3 dependency.
 */

#include "harness/pins/itrace_context.hpp"

namespace Harness {

static uint8_t s_currentTaskId = 0;
static uint8_t s_currentServiceId = 0;

void setTraceTaskId(uint8_t id) {
    s_currentTaskId = id;
}

void setTraceServiceId(uint8_t id) {
    s_currentServiceId = id;
}

uint8_t getTraceTaskId() {
    return s_currentTaskId;
}

uint8_t getTraceServiceId() {
    return s_currentServiceId;
}

} // namespace Harness
