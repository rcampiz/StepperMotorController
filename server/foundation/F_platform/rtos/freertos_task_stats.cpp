/**
 * @file freertos_task_stats.cpp
 * @brief FreeRTOS implementation of ITaskStats
 *
 * Wraps uxTaskGetSystemState() and computes delta-based CPU percentages.
 * All FreeRTOS API usage is confined to this file.
 */

#include "F_platform/rtos/freertos_task_stats.hpp"

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

#include <string.h>

// Map FreeRTOS eTaskState to our platform-neutral state enum
static uint8_t mapState(eTaskState s)
{
    switch (s) {
        case eRunning:   return 0;
        case eReady:     return 1;
        case eBlocked:   return 2;
        case eSuspended: return 3;
        case eDeleted:   return 4;
        default:         return 1; // treat unknown as Ready
    }
}

void FreeRTOSTaskStats::getSnapshot(TaskStatsSnapshot& out)
{
    TaskStatus_t status[MAX_TASKS];
    configRUN_TIME_COUNTER_TYPE totalRunTime = 0;

    UBaseType_t count = uxTaskGetSystemState(status, MAX_TASKS, &totalRunTime);
    if (count > MAX_TASKS) count = MAX_TASKS;

    // Compute delta for CPU%
    uint32_t deltaTotal = totalRunTime - m_prevTotalRunTime;

    out.count = static_cast<uint8_t>(count);
    out.totalCpuCycles = totalRunTime;

    for (UBaseType_t i = 0; i < count; i++) {
        TaskStat& ts = out.tasks[i];

        // Copy name (safely)
        strncpy(ts.name, status[i].pcTaskName, sizeof(ts.name) - 1);
        ts.name[sizeof(ts.name) - 1] = '\0';

        ts.priority = static_cast<uint8_t>(status[i].uxCurrentPriority);
        ts.state = mapState(status[i].eCurrentState);
        ts.stackHWM = static_cast<uint16_t>(status[i].usStackHighWaterMark);

        // Delta-based CPU%
        ts.cpuPercent = 0;
        if (!m_firstSnapshot && deltaTotal > 0) {
            // Find this task in previous snapshot by xTaskNumber
            uint32_t taskNum = status[i].xTaskNumber;
            for (uint8_t j = 0; j < m_prevCount; j++) {
                if (m_prevTaskNumber[j] == taskNum) {
                    uint32_t deltaTask = status[i].ulRunTimeCounter - m_prevRunTime[j];
                    ts.cpuPercent = static_cast<uint8_t>(
                        (deltaTask * 100UL) / deltaTotal);
                    if (ts.cpuPercent > 100) ts.cpuPercent = 100;
                    break;
                }
            }
        }
    }

    // Store current snapshot for next delta computation
    m_prevTotalRunTime = totalRunTime;
    m_prevCount = static_cast<uint8_t>(count);
    for (UBaseType_t i = 0; i < count; i++) {
        m_prevRunTime[i] = status[i].ulRunTimeCounter;
        m_prevTaskNumber[i] = status[i].xTaskNumber;
    }

    // Cache task handle → index mapping
    m_taskCount = static_cast<uint8_t>(count);
    for (UBaseType_t i = 0; i < count; i++) {
        m_taskHandles[i] = static_cast<void*>(status[i].xHandle);
    }

    m_firstSnapshot = false;
}

uint8_t FreeRTOSTaskStats::getCurrentTaskIndex()
{
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (current == nullptr) return 0xFF;

    // Look up by handle directly (more robust than task numbers)
    void* currentPtr = static_cast<void*>(current);
    for (uint8_t i = 0; i < m_taskCount; i++) {
        if (m_taskHandles[i] == currentPtr) {
            return i;
        }
    }

    return 0xFF;
}
