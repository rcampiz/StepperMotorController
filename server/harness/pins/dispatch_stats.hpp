/**
 * @file dispatch_stats.hpp
 * @brief Harness pin: command dispatch statistics (shared POD, owned by no layer)
 *
 * Produced by the L2 parser, read by the L3 dispatcher screen.
 */
#pragma once
#include <stdint.h>

namespace Harness {

struct DispatchStats {
    uint32_t totalCommands;
    uint32_t unknownCommands;
    uint32_t parseErrors;
    uint32_t rxOverflowLines;   ///< lines discarded because the RX ring overflowed during them (F1.1)
    uint8_t  recentCount;
    char     recentCmds[8][24];
};

} // namespace Harness
