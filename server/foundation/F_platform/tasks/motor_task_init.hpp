/**
 * @file motor_task_init.hpp
 * @brief Init contract: create the Motor task bound to an L3 IMotorService
 */
#pragma once

#include "harness/pins/motor_command.hpp"
#include "F_platform/boundary/iqueue.hpp"

namespace Platform { class IClock; class IScheduler; }
namespace Services { class IMotorService; }

namespace Scheduler {

bool initMotorTask(Services::IMotorService& service,
                   Platform::IQueue<Services::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                   Platform::IClock& clock,
                   Platform::IScheduler& scheduler);

} // namespace Scheduler
