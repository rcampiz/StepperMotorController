#include "F_platform/tasks/boundary/idispatch_stats.hpp"

namespace Scheduler {

static IDispatchStats* s_dispatchStatsProvider = nullptr;

void setDispatchStatsProvider(IDispatchStats* provider) {
    s_dispatchStatsProvider = provider;
}

IDispatchStats& dispatchStats() {
    return *s_dispatchStatsProvider;
}

} // namespace Scheduler
