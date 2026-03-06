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
#include "F_platform/types/async_event_types.hpp"

namespace Platform {

using Harness::IClock;
using Harness::ILock;
using Harness::IQueue;
using Harness::ITaskStats;
using Harness::IScheduler;

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
    IScheduler* scheduler;
};

/// Create FreeRTOS primitives. Halts on failure.
void init();

/// Access platform resources as abstract interfaces
const Resources& resources();

} // namespace Platform

#endif // PLATFORM_INIT_HPP
