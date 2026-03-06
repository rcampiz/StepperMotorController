/**
 * @file itask_stats.hpp
 * @brief Abstract task statistics interface
 *
 * Platform abstraction for querying scheduler task information.
 * Implementations may use:
 *   - FreeRTOS uxTaskGetSystemState
 *   - Bare-metal task table
 *   - Stub (unit test)
 *
 * No RTOS headers may appear in this file.
 */

#ifndef PLATFORM_ITASK_STATS_HPP
#define PLATFORM_ITASK_STATS_HPP

#include <stdint.h>

namespace Harness {

/// Per-task statistics snapshot (RTOS-free POD)
struct TaskStat {
    char     name[16];
    uint8_t  priority;
    uint8_t  state;       ///< 0=Running, 1=Ready, 2=Blocked, 3=Suspended, 4=Deleted
    uint16_t stackHWM;    ///< Stack high-water mark (words remaining)
    uint8_t  cpuPercent;  ///< CPU usage 0-100 (delta-based between snapshots)
};

/// Aggregate task statistics
struct TaskStatsSnapshot {
    static constexpr uint8_t MAX_TASKS = 8;
    TaskStat tasks[MAX_TASKS];
    uint8_t  count;           ///< Number of valid entries in tasks[]
    uint32_t totalCpuCycles;  ///< Raw cycle counter (for timeline use)
};

/// Abstract interface for task statistics
class ITaskStats {
public:
    virtual ~ITaskStats() = default;

    /**
     * @brief Fill snapshot with current task statistics
     * @param out Snapshot to populate (caller-owned)
     */
    virtual void getSnapshot(TaskStatsSnapshot& out) = 0;

    /**
     * @brief Get index of the currently executing task
     * @return 0-based index matching the last getSnapshot() ordering, or 0xFF if unknown
     */
    virtual uint8_t getCurrentTaskIndex() = 0;

    /**
     * @brief Get free heap bytes
     * @return Available heap memory in bytes, or 0 if unknown
     */
    virtual uint32_t getFreeHeapBytes() { return 0; }
};

/// Null implementation for testing or when no scheduler is present
class NullTaskStats : public ITaskStats {
public:
    void getSnapshot(TaskStatsSnapshot& out) override { out.count = 0; }
    uint8_t getCurrentTaskIndex() override { return 0xFF; }
};

/// Global accessor (wired by composition root)
void setTaskStatsProvider(ITaskStats* provider);
ITaskStats* taskStatsProvider();

} // namespace Harness

#endif // PLATFORM_ITASK_STATS_HPP
