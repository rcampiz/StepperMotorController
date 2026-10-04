/**
 * @file boundary/itaskstats.hpp
 * @brief Consumer port — Scheduler side of the Platform::ITaskStats harness connection
 *
 * Structurally identical to Platform::ITaskStats.
 * The harness adapter bridges the provider to this consumer port.
 */

#ifndef SCHEDULER_ITASK_STATS_HPP
#define SCHEDULER_ITASK_STATS_HPP

#include <stdint.h>

namespace Scheduler {

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

/// Global accessor (wired by composition root)
void setTaskStatsProvider(ITaskStats* provider);
ITaskStats* taskStatsProvider();

} // namespace Scheduler

#endif // SCHEDULER_ITASK_STATS_HPP
