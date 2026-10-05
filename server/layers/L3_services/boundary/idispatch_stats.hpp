/**
 * @file boundary/idispatch_stats.hpp
 * @brief Dispatch statistics (parsed/ok/error counts) for the heartbeat and DIAG commands (L3-internal interface)
 */

#pragma once

#include <stdint.h>
#include "harness/pins/dispatch_stats.hpp"

namespace Services {

using DispatchStats = Harness::DispatchStats;

class IDispatchStats {
public:
    virtual ~IDispatchStats() = default;
    virtual void getStats(DispatchStats& out) = 0;
};


} // namespace Services
