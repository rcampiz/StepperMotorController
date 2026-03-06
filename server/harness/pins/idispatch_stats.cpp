#include "harness/pins/idispatch_stats.hpp"

namespace Harness {

static IDispatchStats* s_dispatchStatsProvider = nullptr;

void setDispatchStatsProvider(IDispatchStats* provider) {
    s_dispatchStatsProvider = provider;
}

IDispatchStats& dispatchStats() {
    return *s_dispatchStatsProvider;
}

} // namespace Harness
