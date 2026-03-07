/**
 * @file imotor_control_service.hpp
 * @brief Interface for motor control service operations
 *
 * Signal net from F_platform (motor task) down to L3 (MotorController).
 * Implemented directly by MotorController.
 * Covers command processing, periodic control loop, config application,
 * debug readback, safe step mode changes, and reinitialization.
 */

#pragma once

#include "harness/pins/imotor_driver.hpp"
#include "harness/pins/motor_debug_params.hpp"
#include "harness/pins/imotor_command_sink.hpp"

namespace Harness {

class IMotorControlService {
public:
    virtual ~IMotorControlService() = default;

    virtual void processCommand(const MotorCommand& cmd, Drivers::IMotorDriver& driver) = 0;
    virtual void periodicUpdate(Drivers::IMotorDriver& driver) = 0;
    virtual void applyConfig(Drivers::IMotorDriver& driver) = 0;
    virtual bool setStepModeSafe(Drivers::IMotorDriver& driver, uint8_t mode, uint8_t& readback) = 0;
    virtual void reinit(Drivers::IMotorDriver& driver) = 0;
    virtual void getDebugInfo(Drivers::IMotorDriver& driver, MotorDebugParams& info) = 0;
};

} // namespace Harness
