/**
 * @file freertos_task_stats.cpp
 * @brief FreeRTOS implementation of ITaskStats
 *
 * Wraps uxTaskGetSystemState() and computes delta-based CPU percentages.
 * All FreeRTOS API usage is confined to this file.
 */

#include "F_platform/rtos/freertos_task_stats.hpp"
#include "F_platform/rtos/freertos_scheduler.hpp"

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

#include <string.h>
#include <stdint.h>

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

// A task's stack size in words: what the scheduler gave it, else (the idle task) from the
// TCB's stack bounds (configRECORD_STACK_HIGH_ADDRESS). FreeRTOS aligns the top down to 8
// bytes, so the span can come out one word short: rounded up to an even count.
static uint32_t stackWordsOf(const TaskStatus_t& s)
{
    const uint32_t known = Platform::FreeRTOSScheduler::stackWordsOf(s.xHandle);
    if (known != 0) return known;
    if (s.pxEndOfStack == nullptr || s.pxStackBase == nullptr || s.pxEndOfStack < s.pxStackBase) return 0;
    const uint32_t span = static_cast<uint32_t>(s.pxEndOfStack - s.pxStackBase) + 1u;
    return (span + 1u) & ~1u;
}

namespace Platform {

void FreeRTOSTaskStats::getSnapshot(Platform::TaskStatsSnapshot& out)
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
        Platform::TaskStat& ts = out.tasks[i];

        // Copy name (safely)
        strncpy(ts.name, status[i].pcTaskName, sizeof(ts.name) - 1);
        ts.name[sizeof(ts.name) - 1] = '\0';

        ts.priority = static_cast<uint8_t>(status[i].uxCurrentPriority);
        ts.state = mapState(status[i].eCurrentState);
        ts.stackHWM = static_cast<uint16_t>(status[i].usStackHighWaterMark);
        ts.stackWords = static_cast<uint16_t>(stackWordsOf(status[i]));

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

// The per-task tag rides FreeRTOS's application task tag (configUSE_APPLICATION_TASK_TAG):
// stored in each task's TCB, so a preempting task cannot overwrite another's.
bool FreeRTOSTaskStats::setCurrentTaskTag(uint8_t tag)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || xPortIsInsideInterrupt()) return false;
    vTaskSetApplicationTaskTag(nullptr, reinterpret_cast<TaskHookFunction_t>(static_cast<uintptr_t>(tag) + 1u));
    return true;
}

uint8_t FreeRTOSTaskStats::getCurrentTaskTag()
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return 0xFF;
    TaskHookFunction_t raw = xPortIsInsideInterrupt() ? xTaskGetApplicationTaskTagFromISR(nullptr)
                                                      : xTaskGetApplicationTaskTag(nullptr);
    const auto v = reinterpret_cast<uintptr_t>(raw);
    return (v == 0u || v > 256u) ? 0xFF : static_cast<uint8_t>(v - 1u);   // 0: never set
}

uint32_t FreeRTOSTaskStats::getFreeHeapBytes()
{
    // F11: there is no kernel heap any more; report the unused part of the static stack
    // pool so "free_heap" in telemetry still shows allocation headroom (bytes).
    return FreeRTOSScheduler::stackPoolFreeWords() * sizeof(uint32_t);
}

} // namespace Platform
