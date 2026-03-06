/**
 * @file imotor_task_control.hpp
 * @brief Interface for motor task lifecycle operations
 *
 * Signal net from L3 (dispatcher) down to F_platform (motor task).
 * Implemented directly in motor_task.cpp.
 * Covers reinit, config application, debug readback, and safe step mode changes.
 * setStepModeSafe encapsulates the suspend/write/resume pattern.
 */

#pragma once

#include "harness/pins/motor_debug_params.hpp"

namespace Harness {

class IMotorTaskControl {
public:
    virtual ~IMotorTaskControl() = default;

    virtual void reinit() = 0;
    virtual bool applyConfig() = 0;
    virtual bool getDebugInfo(MotorDebugParams& out) = 0;
    virtual bool setStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
};

} // namespace Harness
