/**
 * @file platform_init.hpp
 * @brief FreeRTOS platform resources — isolated from service wiring
 *
 * Owns all FreeRTOS primitives (locks, queues, clock) and exposes them
 * as abstract interfaces. Also wraps xTaskCreate and vTaskStartScheduler.
 * To swap RTOS, rewrite platform_init.cpp only.
 */

#ifndef PLATFORM_INIT_HPP
#define PLATFORM_INIT_HPP

#include "F_platform/hal/iclock.hpp"
#include "F_platform/hal/ilock.hpp"
#include "F_platform/hal/iqueue.hpp"
#include "F_platform/hal/itask_stats.hpp"
#include "F_platform/types/async_event_types.hpp"

namespace Platform {

struct Resources {
    ILock*  spi1Lock;
    ILock*  spi2Lock;
    ILock*  controlModeLock;
    ILock*  commandQueueLock;
    ILock*  uiModeLock;
    ILock*  telemetryLock;
    IClock* clock;
    IQueue<AsyncEvent, 8>* eventQueue;
    ITaskStats* taskStats;
};

/// Create FreeRTOS primitives. Halts on failure.
void init();

/// Access platform resources as abstract interfaces
const Resources& resources();

/// Create FreeRTOS tasks (encoder created only if available)
void createTasks(bool encoderAvailable);

/// Start FreeRTOS scheduler — never returns
[[noreturn]] void startScheduler();

} // namespace Platform

#endif // PLATFORM_INIT_HPP
