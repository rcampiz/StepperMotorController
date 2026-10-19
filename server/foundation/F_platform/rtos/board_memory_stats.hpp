/**
 * @file board_memory_stats.hpp
 * @brief The STM32F401RE memory map behind Platform::IMemoryStats: linker symbols for flash and
 *        static RAM, the FreeRTOS stack pool, and a painted main stack (MSP) for its peak.
 */

#ifndef PLATFORM_BOARD_MEMORY_STATS_HPP
#define PLATFORM_BOARD_MEMORY_STATS_HPP

#include "F_platform/boundary/imemory_stats.hpp"

namespace Platform {

class BoardMemoryStats : public IMemoryStats {
public:
    bool getMemoryStats(MemoryStats& out) override;
};

/**
 * @brief Paint the free main-stack region (so its peak can be read later) and return the
 *        provider. Call first thing from the composition root, on the main stack, before the
 *        scheduler starts: the peak then covers the init cascades and every interrupt after.
 */
IMemoryStats& initMemoryStats();

} // namespace Platform

#endif // PLATFORM_BOARD_MEMORY_STATS_HPP
