/**
 * @file freertos_task_stats.hpp
 * @brief FreeRTOS implementation of ITaskStats
 *
 * Uses uxTaskGetSystemState() for task enumeration and
 * DWT CYCCNT-based runtime stats for delta CPU% computation.
 *
 * This file includes FreeRTOS headers — must NOT be included
 * outside the platform layer.
 */

#ifndef FREERTOS_TASK_STATS_HPP
#define FREERTOS_TASK_STATS_HPP

#include "F_platform/interfaces/itask_stats.hpp"
#include <stdint.h>

class FreeRTOSTaskStats : public ITaskStats {
public:
    void getSnapshot(TaskStatsSnapshot& out) override;
    uint8_t getCurrentTaskIndex() override;

private:
    static constexpr uint8_t MAX_TASKS = TaskStatsSnapshot::MAX_TASKS;

    // Previous snapshot for delta-based CPU% computation
    uint32_t m_prevRunTime[MAX_TASKS] = {};
    uint32_t m_prevTaskNumber[MAX_TASKS] = {};
    uint32_t m_prevTotalRunTime = 0;
    uint8_t  m_prevCount = 0;
    bool     m_firstSnapshot = true;

    // Cached task handle → index mapping (stable across snapshots)
    void* m_taskHandles[MAX_TASKS] = {};
    uint8_t  m_taskCount = 0;
};

#endif // FREERTOS_TASK_STATS_HPP
