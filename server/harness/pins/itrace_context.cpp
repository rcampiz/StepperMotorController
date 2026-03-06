/**
 * @file itrace_context.cpp
 * @brief Trace context setter delegation
 *
 * Forwards harness-level trace context setters to the L3 Trace module.
 * Linked once; called from Scheduler tasks.
 */

#include "harness/pins/itrace_context.hpp"
#include "L3_services/infra/trace/trace.hpp"

namespace Harness {

void setTraceTaskId(uint8_t id) {
    Trace::setCurrentTaskId(id);
}

void setTraceServiceId(uint8_t id) {
    Trace::setCurrentServiceId(id);
}

} // namespace Harness
