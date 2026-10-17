/**
 * @file platform_init.hpp
 * @brief FreeRTOS platform resources — isolated from service wiring
 *
 * Owns all FreeRTOS primitives (locks, queues, clock, scheduler) and
 * exposes them as abstract interfaces.
 * To swap RTOS, rewrite platform_init.cpp only.
 */

#ifndef PLATFORM_INIT_HPP
#define PLATFORM_INIT_HPP

#include "harness/pins/iclock.hpp"
#include "harness/pins/ilock.hpp"
#include "harness/pins/iqueue.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/pins/async_event.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include "F_platform/boundary/icritical_section.hpp"
#include "F_platform/boundary/itask_notifier.hpp"

namespace Platform {

struct Resources {
    ILock*  spi1Lock;
    ILock*  spi2Lock;
    ILock*  controlModeLock;
    ILock*  commandQueueLock;
    ILock*  uiModeLock;
    ILock*  telemetryLock;
    ILock*  deviceConfigLock;
    IClock* clock;
    IQueue<AsyncEvent, 8>* eventQueue;
    ITaskStats* taskStats;
    IScheduler* scheduler;
    IMicrosecondClock* usClock;
    ICriticalSection* critical;
    ITaskNotifier* notifier;
};

/// Create FreeRTOS primitives. Halts on failure.
void init();

/// Access platform resources as abstract interfaces
const Resources& resources();

} // namespace Platform

#endif // PLATFORM_INIT_HPP
