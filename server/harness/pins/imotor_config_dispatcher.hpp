/**
 * @file imotor_config_dispatcher.hpp
 * @brief Interface for motor configuration operations (DRV:CFG namespace)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Harness {

class IMotorConfigDispatcher {
public:
    virtual ~IMotorConfigDispatcher() = default;

    // Physical unit setters
    virtual ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) = 0;
    virtual ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) = 0;
    virtual ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) = 0;

    // Motor config queries
    virtual uint32_t getMicrostepsPerRev() = 0;
    virtual uint16_t getFullStepsPerRev() = 0;

    // Flash persistence
    virtual bool configSaveToFlash() = 0;
    virtual bool configLoadFromFlash() = 0;
    virtual bool configFactoryReset() = 0;
    virtual bool configIsValid() = 0;

    // DRV register setters
    virtual void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) = 0;
    virtual void configSetOcdThreshold(uint8_t thresh) = 0;
    virtual void configSetStallThreshold(uint8_t thresh) = 0;
    virtual void configSetFaultAction(uint8_t action) = 0;
    virtual void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                            bool uvlo, bool stallA, bool stallB, bool cmdErr) = 0;
    virtual void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) = 0;
    virtual void configSetStepMode(uint8_t mode) = 0;
    virtual void configSetFullStepsPerRev(uint16_t steps) = 0;
    virtual void configSetEncoderPPR(uint16_t ppr) = 0;
    virtual uint16_t configGetEncoderPPR() = 0;

    // Config snapshot (L2 owns formatting)
    struct MotorConfigSnapshot {
        uint8_t kvalHold, kvalRun, kvalAcc, kvalDec;
        uint8_t ocdThreshold, stallThreshold;
        uint16_t acceleration, deceleration, maxSpeed, minSpeed, fsSpeed;
        bool faultOcd, faultThermalSD, faultThermalWarn, faultUvlo;
        bool faultStallA, faultStallB, faultCmdErr;
        uint8_t faultAction;
        uint8_t stepMode;
        bool valid;
    };
    virtual MotorConfigSnapshot getMotorConfig() = 0;
};

} // namespace Harness
