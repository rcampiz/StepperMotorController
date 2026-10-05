/**
 * @file motor_service_init.hpp
 * @brief Services::initMotorSubsystem() — L3 entry point for the motor cascade
 */
#pragma once

#include "harness/pins/motor_command.hpp"
#include "F_platform/boundary/iqueue.hpp"

namespace Platform { class IClock; class IScheduler; class ILock; class ITaskStats; }

namespace Services {

class IMotorTaskControl;

struct MotorSubsystemResult {
    Services::IMotorTaskControl* motorCtrl = nullptr;  ///< Motor task control interface (MotorService)
};

bool initMotorSubsystem(
    Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Platform::IClock& clock,
    Platform::IScheduler& scheduler,
    Platform::ILock& commandQueueLock,
    Platform::ITaskStats* taskStats,
    MotorSubsystemResult& result);

} // namespace Services
