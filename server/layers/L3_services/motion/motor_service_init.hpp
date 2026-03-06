/**
 * @file motor_service_init.hpp
 * @brief Harness contract for L3 motor subsystem init
 *
 * L3 orchestrates: L4 driver (through harness) → L3 config →
 * task (through harness) → command queue → adapter wiring.
 * All Platform deps injected — no Platform::resources() calls.
 */

#pragma once

#include "harness/pins/motor_task_init.hpp"

namespace Harness {
class IMotorTaskControl;
class IClock;
class IScheduler;
class ILock;
} // namespace Harness

namespace Services {

/**
 * @brief Result of motor subsystem initialization
 */
struct MotorSubsystemResult {
    Harness::IMotorTaskControl* motorCtrl = nullptr;  ///< Motor task control interface
};

/**
 * @brief Initialize motor subsystem — L3 drives the cascade
 *
 * @param commandQueue Motor command queue (from Platform)
 * @param clock        System clock (injected from Platform)
 * @param scheduler    Scheduler for RTOS task creation (injected from Platform)
 * @param commandQueueLock Lock for command queue service (injected from Platform)
 * @param result       Output: motor task interfaces
 * @return true on success
 */
bool initMotorSubsystem(
    Harness::IQueue<Harness::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>& commandQueue,
    Harness::IClock& clock,
    Harness::IScheduler& scheduler,
    Harness::ILock& commandQueueLock,
    MotorSubsystemResult& result);

} // namespace Services
