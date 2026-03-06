/**
 * @file imotor_driver_dispatcher.hpp
 * @brief Interface for motor driver management (MOTOR_REINIT, DRV namespace)
 */

#pragma once

#include "harness/pins/motor_debug_params.hpp"
#include <stdint.h>

namespace Harness {

class IMotorDriverDispatcher {
public:
    virtual ~IMotorDriverDispatcher() = default;

    struct FaultEnableFlags {
        bool ocd;
        bool thermalSD;
        bool thermalWarn;
        bool uvlo;
        bool stallA;
        bool stallB;
        bool cmdErr;
    };

    virtual void motorReinit() = 0;
    virtual bool motorApplyConfig() = 0;
    virtual bool motorGetDebugInfo(MotorDebugParams& out) = 0;
    virtual bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
    virtual FaultEnableFlags getFaultEnable() = 0;
};

} // namespace Harness
