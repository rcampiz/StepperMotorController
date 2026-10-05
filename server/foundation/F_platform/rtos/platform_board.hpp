/**
 * @file platform_board.hpp
 * @brief Harness contract for RTOS platform initialization
 *
 * Declares PlatformResources struct and init function.
 * F_platform provides the implementation (FreeRTOS objects).
 * Composition root calls through this harness interface.
 */

#pragma once

#include "harness/pins/iclock.hpp"
#include "harness/pins/ilock.hpp"
#include "harness/pins/iqueue.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/pins/motor_command.hpp"
#include "harness/pins/async_event.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include "F_platform/boundary/icritical_section.hpp"
#include "F_platform/boundary/itask_notifier.hpp"

namespace Harness {

/**
 * @brief Platform resources — abstract interface pointers to RTOS primitives
 */
struct PlatformResources {
    Platform::ILock*  spi1Lock;
    Platform::ILock*  spi2Lock;
    Platform::ILock*  controlModeLock;
    Platform::ILock*  commandQueueLock;
    Platform::ILock*  uiModeLock;
    Platform::ILock*  telemetryLock;
    Platform::IClock* clock;
    Platform::IQueue<AsyncEvent, 8>* eventQueue;
    Platform::ITaskStats* taskStats;
    Platform::IScheduler* scheduler;
    Platform::IQueue<Scheduler::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>* motorCmdQueue;
    Platform::IMicrosecondClock* usClock;
    Platform::ICriticalSection* critical;
    Platform::ITaskNotifier* notifier;
};

/**
 * @brief Initialize RTOS platform and create all primitives
 *
 * Creates locks, clocks, queues, scheduler — all as abstract interfaces.
 * Implemented by F_platform (FreeRTOS).
 *
 * @param out Output struct populated with interface pointers
 * @return true on success
 */
bool initPlatformBoard(PlatformResources& out);

} // namespace Harness
