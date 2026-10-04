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

#include "L3_services/boundary/imotor_driver.hpp"
#include "harness/pins/motor_debug_params.hpp"
#include "L3_services/boundary/imotor_command_sink.hpp"

namespace Services {

class IMotorControlService {
public:
    virtual ~IMotorControlService() = default;

    virtual void processCommand(const Services::MotorCommand& cmd, Services::IMotorDriver& driver) = 0;
    virtual void periodicUpdate(Services::IMotorDriver& driver) = 0;
    virtual void applyConfig(Services::IMotorDriver& driver) = 0;
    virtual bool setStepModeSafe(Services::IMotorDriver& driver, uint8_t mode, uint8_t& readback) = 0;
    virtual void reinit(Services::IMotorDriver& driver) = 0;
    virtual void getDebugInfo(Services::IMotorDriver& driver, Harness::MotorDebugParams& info) = 0;
};

} // namespace Services
