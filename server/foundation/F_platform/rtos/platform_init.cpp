/**
 * @file platform_init.cpp
 * @brief FreeRTOS platform — creates primitives, tasks, starts scheduler
 *
 * All FreeRTOS API calls are isolated here. To swap RTOS, rewrite this file.
 */

#include "F_platform/rtos/platform_init.hpp"
#include "F_platform/hw/early_debug.hpp"
#include "F_platform/rtos/freertos_lock.hpp"
#include "F_platform/rtos/freertos_clock.hpp"
#include "F_platform/rtos/freertos_queue.hpp"
#include "F_platform/rtos/freertos_task_stats.hpp"

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

#ifdef ENABLE_SEGGER_SYSTEMVIEW
#include "X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.h"
#endif

#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "F_platform/tasks/motor_task.hpp"

// ============================================================================
// Static FreeRTOS objects (created once, never destroyed)
// ============================================================================

static FreeRTOSMutex s_spi1Lock;
static FreeRTOSMutex s_spi2Lock;
static FreeRTOSMutex s_controlModeLock;
static FreeRTOSMutex s_commandQueueLock;
static FreeRTOSMutex s_uiModeLock;
static FreeRTOSMutex s_telemetryLock;
static FreeRTOSClock s_clock;
static FreeRTOSQueue<AsyncEvent, 8> s_eventQueue;
static FreeRTOSTaskStats s_taskStats;

static Platform::Resources s_resources;

// ============================================================================
// Platform API
// ============================================================================

namespace Platform {

void init()
{
    if (!s_spi1Lock.valid() || !s_spi2Lock.valid() ||
        !s_controlModeLock.valid() || !s_commandQueueLock.valid() ||
        !s_uiModeLock.valid() || !s_telemetryLock.valid()) {
        EarlyDebug::println("Platform locks: FAIL!");
        while (1) {}
    }

    s_resources = {
        &s_spi1Lock, &s_spi2Lock,
        &s_controlModeLock, &s_commandQueueLock,
        &s_uiModeLock, &s_telemetryLock,
        &s_clock, &s_eventQueue,
        &s_taskStats
    };
}

const Resources& resources()
{
    return s_resources;
}

void createTasks(bool encoderAvailable)
{
    EarlyDebug::println("Creating FreeRTOS tasks...");

    if (encoderAvailable) {
        xTaskCreate(Tasks::vEncoderTask, "Encoder",
                    Tasks::ENCODER_TASK_STACK_SIZE, nullptr,
                    Tasks::ENCODER_TASK_PRIORITY, nullptr);
    }

    xTaskCreate(Tasks::vMotorTask, "Motor",
                Tasks::MOTOR_TASK_STACK_SIZE, nullptr,
                Tasks::MOTOR_TASK_PRIORITY, &Tasks::g_motorTaskHandle);

    xTaskCreate(Tasks::vDisplayTask, "Display",
                Tasks::DISPLAY_TASK_STACK_SIZE, nullptr,
                Tasks::DISPLAY_TASK_PRIORITY, &Tasks::g_displayTaskHandle);

    xTaskCreate(Tasks::vCommsTask, "Comms",
                Tasks::COMMS_TASK_STACK_SIZE, nullptr,
                Tasks::COMMS_TASK_PRIORITY, nullptr);
}

void startScheduler()
{
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();   // Init RTT channel 1 buffer + system description
    SEGGER_SYSVIEW_Start();
#endif

    EarlyDebug::println("Starting scheduler...");
    EarlyDebug::println("===================");

    vTaskStartScheduler();

    EarlyDebug::println("!!! SCHEDULER RETURNED !!!");
    while (1) {}
}

} // namespace Platform
