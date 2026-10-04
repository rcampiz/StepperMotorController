/**
 * @file boundary/idispatchstats.hpp
 * @brief Consumer port — Services side of the Scheduler::IDispatchStats harness connection
 *
 * Structurally identical to Scheduler::IDispatchStats.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Services {

struct DispatchStats {
    uint32_t totalCommands;
    uint32_t unknownCommands;
    uint32_t parseErrors;
    uint8_t  recentCount;
    char     recentCmds[8][24];
};

class IDispatchStats {
public:
    virtual ~IDispatchStats() = default;
    virtual void getStats(DispatchStats& out) = 0;
};


} // namespace Services
