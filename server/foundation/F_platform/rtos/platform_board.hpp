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
#include "harness/pins/imotor_command_sink.hpp"
#include "F_platform/tasks/motor_task_init.hpp"
#include "F_platform/types/async_event_types.hpp"

namespace Harness {

/**
 * @brief Platform resources — abstract interface pointers to RTOS primitives
 */
struct PlatformResources {
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
    IQueue<MotorCommand, MOTOR_CMD_QUEUE_DEPTH>* motorCmdQueue;
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
