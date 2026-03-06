/**
 * @file comms_task_init.hpp
 * @brief Harness init contract — comms task init + RTOS task creation
 *
 * Wraps Scheduler::CommsTask_Init() + scheduler->createTask() +
 * SafetyActionsImpl adapter + joystick callback registration.
 *
 * Declared in harness so L3 can init the comms task without including
 * F_platform/tasks headers.
 */

#pragma once

#include <stdint.h>

namespace Harness {
class ITransport;
class IClock;
class IMotorEventReceiver;
class IEmergencyStop;
class IScheduler;
class ICommandProcessor;
} // namespace Harness

namespace Harness {

/**
 * @brief Result of comms task initialization
 */
struct CommsTaskResult {
    uint32_t (*getEventSeqFn)() = nullptr;  ///< Wire event sequence accessor
};

/**
 * @brief Initialize comms task, safety adapter, and create RTOS task
 *
 * Wraps Scheduler::CommsTask_Init() + SafetyActionsImpl adapter +
 * CommsTask_RegisterJoyCallback() + scheduler->createTask().
 *
 * @param transport     Byte-level I/O (from L1/L2 cascade)
 * @param parser        Command parser (from L2 init)
 * @param clock         System clock (from L1/L2 cascade)
 * @param eventReceiver Motor event receiver (L3)
 * @param emergencyStop Emergency stop interface (L3)
 * @param scheduler     Scheduler for RTOS task creation
 * @param result        Output: comms interfaces (populated on success)
 * @return true on success
 */
bool initCommsTask(ITransport& transport,
                   ICommandProcessor& parser,
                   IClock& clock,
                   IMotorEventReceiver& eventReceiver,
                   IEmergencyStop& emergencyStop,
                   IScheduler& scheduler,
                   CommsTaskResult& result);

} // namespace Harness
