/**
 * @file boundary/imotordriverdispatcher.hpp
 * @brief Consumer port — Protocol side of the Services::IMotorDriverDispatcher harness connection
 *
 * Structurally identical to Services::IMotorDriverDispatcher.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/motor_debug_params.hpp"
#include <stdint.h>

namespace Protocol {

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
    virtual bool motorGetDebugInfo(Harness::MotorDebugParams& out) = 0;
    virtual bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
    virtual FaultEnableFlags getFaultEnable() = 0;
};

} // namespace Protocol
