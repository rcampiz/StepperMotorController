#include "harness/pins/itask_stats.hpp"

namespace Platform {

static ITaskStats* s_taskStatsProvider = nullptr;

void setTaskStatsProvider(ITaskStats* provider) {
    s_taskStatsProvider = provider;
}

ITaskStats* taskStatsProvider() {
    return s_taskStatsProvider;
}

} // namespace Platform
