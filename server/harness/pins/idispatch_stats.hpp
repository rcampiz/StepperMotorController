/**
 * @file idispatch_stats.hpp
 * @brief Dispatch statistics interface — boundary contract
 *
 * L3 screens read command dispatch stats through this interface
 * without including F_platform/tasks/comms_task.hpp.
 */

#pragma once

#include <stdint.h>

namespace Harness {

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

void setDispatchStatsProvider(IDispatchStats* provider);
IDispatchStats& dispatchStats();

} // namespace Harness
