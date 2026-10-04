/**
 * @file boundary/imotortaskcontrol.hpp
 * @brief Consumer port — Services side of the Scheduler::IMotorTaskControl harness connection
 *
 * Structurally identical to Scheduler::IMotorTaskControl.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/motor_debug_params.hpp"

namespace Services {

class IMotorTaskControl {
public:
    virtual ~IMotorTaskControl() = default;

    virtual void reinit() = 0;
    virtual bool applyConfig() = 0;
    virtual bool getDebugInfo(Harness::MotorDebugParams& out) = 0;
    virtual bool setStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
};

} // namespace Services
