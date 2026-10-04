/**
 * @file itask_stats.cpp
 * @brief Scheduler::ITaskStats accessor implementation
 */

#include "F_platform/tasks/boundary/itask_stats.hpp"

namespace Scheduler {

static ITaskStats* s_taskStatsProvider = nullptr;

void setTaskStatsProvider(ITaskStats* provider) {
    s_taskStatsProvider = provider;
}

ITaskStats* taskStatsProvider() {
    return s_taskStatsProvider;
}

} // namespace Scheduler
