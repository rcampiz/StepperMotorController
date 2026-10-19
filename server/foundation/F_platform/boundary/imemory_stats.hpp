/**
 * @file imemory_stats.hpp
 * @brief Abstract memory statistics: where the flash and the RAM go.
 *
 * Platform knowledge (linker symbols, the RTOS stack pool, the main stack) behind an
 * RTOS-free interface, so L2 (DIAG:MEM?) and L3 (the LCD Memory page) read it like any other
 * power-net primitive (rule 4). Implementations:
 *   - the board one (F_platform/rtos/board_memory_stats.cpp): linker symbols + stack pool
 *   - NullMemoryStats / none: host builds (tests, the virtual Nucleo)
 *
 * No RTOS headers may appear in this file.
 */

#ifndef PLATFORM_IMEMORY_STATS_HPP
#define PLATFORM_IMEMORY_STATS_HPP

#include <stdint.h>

namespace Platform {

/// Memory use in bytes (RTOS-free POD). A total of 0 means "not known on this platform".
struct MemoryStats {
    uint32_t flashUsed;       ///< code + constants + the .data load image
    uint32_t flashTotal;      ///< the FLASH region of the linker script
    uint32_t ramStatic;       ///< .data + .bss (the task stack pool is part of it)
    uint32_t ramTotal;        ///< the RAM region of the linker script
    uint32_t stackPoolUsed;   ///< part of the task stack pool handed to tasks
    uint32_t stackPoolTotal;  ///< the whole pool (static, in .bss)
    uint32_t mainStackUsed;   ///< main stack (MSP: boot, then interrupts): deepest use since boot
    uint32_t mainStackTotal;  ///< the linker's reserve for it (used may exceed it: no guard)
};

/// Abstract interface for memory statistics
class IMemoryStats {
public:
    virtual ~IMemoryStats() = default;

    /**
     * @brief Fill `out` with the current figures
     * @return false when nothing is known (out is zeroed)
     */
    virtual bool getMemoryStats(MemoryStats& out) = 0;
};

/// Null implementation: hosts without a memory map
class NullMemoryStats : public IMemoryStats {
public:
    bool getMemoryStats(MemoryStats& out) override { out = MemoryStats{}; return false; }
};

/// Global accessor (wired by the composition root). Header-only, so every build that
/// includes this header links it; nullptr until wired (host tests, the virtual Nucleo).
inline IMemoryStats*& memoryStatsSlot() { static IMemoryStats* s_provider = nullptr; return s_provider; }
inline void setMemoryStatsProvider(IMemoryStats* provider) { memoryStatsSlot() = provider; }
inline IMemoryStats* memoryStatsProvider() { return memoryStatsSlot(); }

} // namespace Platform

#endif // PLATFORM_IMEMORY_STATS_HPP
