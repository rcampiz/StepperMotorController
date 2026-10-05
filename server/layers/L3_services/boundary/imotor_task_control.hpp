/**
 * @file boundary/imotor_task_control.hpp
 * @brief Motor task-side control: suspend/resume around step-mode changes, debug params (L3-internal interface)
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
