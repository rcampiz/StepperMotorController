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
    /// Put the saved driver settings in force. Settings the driver takes only with the
    /// outputs off are applied only if they already are; false when some are not in force
    /// (the caller then uses applyConfigOutputsOff()).
    virtual bool applyConfig() = 0;
    /// The saved settings with the outputs released for the write, then energised again
    virtual bool applyConfigOutputsOff() = 0;
    /// The settings in force with a new resolution (microsteps per full step), outputs
    /// released for the write; `effective` = the resolution the driver reads back
    virtual bool setMicrostepsSafe(uint16_t microsteps, uint16_t& effective) = 0;
    virtual void reinit() = 0;
    virtual void getDebugInfo(Harness::MotorDebugParams& info, Harness::DriverDiagView view) = 0;
};

} // namespace Services
