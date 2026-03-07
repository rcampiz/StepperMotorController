/**
 * @file motor_task_init.hpp
 * @brief Harness init contract — motor task init + RTOS task creation
 *
 * Declared in harness so L3 can init the motor task without including
 * F_platform/tasks headers. Implemented alongside motor_task.cpp.
 */

#pragma once

#include <stdint.h>
#include "harness/pins/iqueue.hpp"
#include "harness/pins/imotor_command_sink.hpp"

namespace Drivers { class IMotorDriver; }

namespace Platform { class IClock; class IScheduler; }

namespace Harness {
class IMotorControlService;
class IMotorTaskControl;

constexpr size_t MOTOR_CMD_QUEUE_DEPTH = 8;

/**
 * @brief Result of motor task initialization
 */
struct MotorTaskResult {
    IMotorTaskControl* motorCtrl = nullptr;  ///< Motor task control interface
};

/**
 * @brief Initialize motor task and create RTOS task
 *
 * Wraps Scheduler::MotorTask_Init() + scheduler->createTask().
 * L3 calls this through the harness — no F_platform/tasks include needed.
 *
 * @param driver Motor driver (from L4 init)
 * @param cmdQueue Command queue (from Platform)
 * @param clock System clock (from Platform bus nets)
 * @param controller Motor control service (L3)
 * @param scheduler Scheduler for RTOS task creation
 * @param result Output: motor task interfaces (populated on success)
 * @return true on success
 */
bool initMotorTask(Drivers::IMotorDriver& driver,
                   Platform::IQueue<MotorCommand, MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                   Platform::IClock& clock,
                   IMotorControlService& controller,
                   Platform::IScheduler& scheduler,
                   MotorTaskResult& result);

} // namespace Harness
