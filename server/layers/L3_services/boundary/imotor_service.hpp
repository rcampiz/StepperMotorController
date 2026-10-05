/**
 * @file imotor_service.hpp
 * @brief Services:: provider — Motor task entry points (H_TASKS_L3)
 *
 * The Motor task only waits on the Platform command queue (50 ms timeout) and calls
 * onCommand() / periodicUpdate(). The driver, the controller, the status polling and the
 * telemetry writes all live inside Services::MotorService.
 */
#pragma once
#include "harness/pins/motor_command.hpp"

namespace Services {

class IMotorService {
public:
    virtual ~IMotorService() = default;

    /// A command was received from the queue. Motor task context.
    virtual void onCommand(const MotorCommand& cmd) = 0;

    /// Periodic status poll (every 50 ms, or right after a GetStatus command). Motor task context.
    virtual void periodicUpdate() = 0;
};

} // namespace Services
