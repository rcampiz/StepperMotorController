/**
 * @file platform_init.cpp
 * @brief FreeRTOS platform — creates primitives, exposes abstract interfaces
 *
 * All FreeRTOS object creation is isolated here. To swap RTOS, rewrite this file.
 */

#include "F_platform/rtos/platform_init.hpp"
#include "F_platform/rtos/platform_board.hpp"
#include "F_platform/hw/early_debug.hpp"
#include "F_platform/rtos/freertos_lock.hpp"
#include "F_platform/rtos/freertos_clock.hpp"
#include "F_platform/rtos/freertos_queue.hpp"
#include "F_platform/rtos/freertos_task_stats.hpp"
#include "F_platform/rtos/freertos_scheduler.hpp"

// ============================================================================
// Static FreeRTOS objects (created once, never destroyed)
// ============================================================================

static Platform::FreeRTOSMutex s_spi1Lock;
static Platform::FreeRTOSMutex s_spi2Lock;
static Platform::FreeRTOSMutex s_controlModeLock;
static Platform::FreeRTOSMutex s_commandQueueLock;
static Platform::FreeRTOSMutex s_uiModeLock;
static Platform::FreeRTOSMutex s_telemetryLock;
static Platform::FreeRTOSClock s_clock;
static Platform::FreeRTOSQueue<AsyncEvent, 8> s_eventQueue;
static Platform::FreeRTOSTaskStats s_taskStats;
static Platform::FreeRTOSScheduler s_scheduler;
static Platform::FreeRTOSQueue<Harness::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH> s_motorCmdQueue;

static Platform::Resources s_resources;

// ============================================================================
// Platform API (internal — used by task_monitor_screen)
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
        &s_taskStats, &s_scheduler
    };
}

const Resources& resources()
{
    return s_resources;
}

} // namespace Platform

// ============================================================================
// Harness contract implementation
// ============================================================================

namespace Harness {

bool initPlatformBoard(PlatformResources& out)
{
    Platform::init();

    out.spi1Lock         = &s_spi1Lock;
    out.spi2Lock         = &s_spi2Lock;
    out.controlModeLock  = &s_controlModeLock;
    out.commandQueueLock = &s_commandQueueLock;
    out.uiModeLock       = &s_uiModeLock;
    out.telemetryLock    = &s_telemetryLock;
    out.clock            = &s_clock;
    out.eventQueue       = &s_eventQueue;
    out.taskStats        = &s_taskStats;
    out.scheduler        = &s_scheduler;
    out.motorCmdQueue    = &s_motorCmdQueue;

    return true;
}

} // namespace Harness
