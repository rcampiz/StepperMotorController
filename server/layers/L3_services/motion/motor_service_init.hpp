/**
 * @file motor_service_init.hpp
 * @brief L3 init contract: bind the motor driver, config store, MotorService and CommandQueue
 *
 * Creates no task. The composition root hands the returned IMotorService to the task sheet.
 */
#pragma once

#include "harness/pins/motor_command.hpp"
#include "F_platform/boundary/iqueue.hpp"

namespace Platform { class IClock; class ILock; class ITaskStats; }

namespace Services {

class IMotorService;
class IMotorTaskControl;

struct MotorSubsystemResult {
    Services::IMotorService*     motorService = nullptr;  ///< Motor task entry (H_TASKS_L3)
    Services::IMotorTaskControl* motorCtrl = nullptr;     ///< Comms-side control (MotorService)
};

bool initMotorSubsystem(
    Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Platform::IClock& clock,
    Platform::ILock& commandQueueLock,
    Platform::ITaskStats* taskStats,
    MotorSubsystemResult& result);

} // namespace Services
