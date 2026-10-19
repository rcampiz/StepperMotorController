/**
 * @file itrace_context.cpp
 * @brief Trace context storage — owns task/service ID statics
 *
 * The task id is kept per task by the scheduler (Platform::ITaskStats task tag): a plain
 * global was overwritten by every preempting task, so the preempted one reported the wrong
 * id. The global is only the fallback when no scheduler keeps tags (host builds, pre-start).
 * The service id stays a global (set and read within one task's call). No L3 dependency.
 */

#include "harness/pins/itrace_context.hpp"
#include "F_platform/boundary/itask_stats.hpp"

namespace Harness {

static uint8_t s_currentTaskId = 0;
static uint8_t s_currentServiceId = 0;

void setTraceTaskId(uint8_t id) {
    s_currentTaskId = id;
    Platform::ITaskStats* stats = Platform::taskStatsProvider();
    if (stats != nullptr) (void)stats->setCurrentTaskTag(id);
}

void setTraceServiceId(uint8_t id) {
    s_currentServiceId = id;
}

uint8_t getTraceTaskId() {
    Platform::ITaskStats* stats = Platform::taskStatsProvider();
    if (stats != nullptr) {
        const uint8_t tag = stats->getCurrentTaskTag();
        if (tag != 0xFF) return tag;
    }
    return s_currentTaskId;
}

uint8_t getTraceServiceId() {
    return s_currentServiceId;
}

} // namespace Harness
