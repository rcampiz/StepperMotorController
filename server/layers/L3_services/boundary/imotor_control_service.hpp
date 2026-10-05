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

#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/motor_debug_params.hpp"
#include "L3_services/boundary/imotor_command_sink.hpp"

namespace Services {

class IMotorControlService {
public:
    virtual ~IMotorControlService() = default;

    virtual void processCommand(const Services::MotorCommand& cmd) = 0;
    virtual void periodicUpdate() = 0;
    virtual void applyConfig() = 0;
    virtual bool setStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
    virtual void reinit() = 0;
    virtual void getDebugInfo(Harness::MotorDebugParams& info) = 0;
};

} // namespace Services
