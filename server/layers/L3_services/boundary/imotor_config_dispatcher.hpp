/**
 * @file imotor_config_dispatcher.hpp
 * @brief Interface for motor configuration operations (DRV:CFG namespace)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include "harness/pins/driver_settings.hpp"
#include <stdint.h>

namespace Services {

class IMotorConfigDispatcher {
public:
    virtual ~IMotorConfigDispatcher() = default;

    // Physical unit setters
    virtual Harness::ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) = 0;
    virtual Harness::ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) = 0;
    virtual Harness::ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) = 0;

    // Motor config queries
    /// What the motor driver can do: protocol limits come from here, not from chip constants
    virtual Harness::DriverCaps driverCaps() = 0;
    virtual uint32_t getMicrostepsPerRev() = 0;
    virtual uint16_t getFullStepsPerRev() = 0;

    // Flash persistence
    /// Ok, StoreFull (journal full while the motor moves: stop and save again) or FlashError.
    virtual Harness::ServiceStatus configSaveToFlash() = 0;
    virtual bool configLoadFromFlash() = 0;
    virtual bool configFactoryReset() = 0;
    virtual bool configIsValid() = 0;

    // Protocol 2 chip-unit setters (DRV:CFG:KVAL/TVAL/OCD/STALL/MOTION/PRED/DECAY/FAULT): kept
    // one release as aliases. L3 asks the driver to translate them into the physical settings
    // (IMotorDriver::fromLegacyUnits); a driver without these units ignores them.
    virtual void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) = 0;
    /// 0 = voltage mode (duty), 1 = current mode (current references); the driver applies it with the outputs off
    virtual void configSetDriveMode(uint8_t mode) = 0;
    virtual void configSetTval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) = 0;
    /// Current-mode regulation: PRED_EN, TSW (0..31, 0xFF = chip default), T_FAST / TON_MIN /
    /// TOFF_MIN (0 = chip default); CONFIG bits go through the Hi-Z path on APPLY
    virtual void configSetCmRegulation(bool predEn, uint8_t tsw, uint8_t tFast, uint8_t tonMin, uint8_t toffMin) = 0;
    /// Coils to Hi-Z after ms stopped (0 = never); takes effect at once, no APPLY needed
    virtual void configSetIdleRelease(uint16_t ms) = 0;
    virtual void configSetOcdThreshold(uint8_t thresh) = 0;
    virtual void configSetStallThreshold(uint8_t thresh) = 0;
    virtual void configSetFaultAction(uint8_t action) = 0;
    virtual void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                            bool uvlo, bool stallA, bool stallB, bool cmdErr) = 0;
    virtual void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) = 0;
    /// Step mode = log2 of the microsteps per full step asked for (the driver caps it)
    virtual void configSetStepMode(uint8_t mode) = 0;
    virtual void configSetFullStepsPerRev(uint16_t steps) = 0;
    virtual void configSetEncoderPPR(uint16_t ppr) = 0;
    virtual uint16_t configGetEncoderPPR() = 0;

    // Config snapshot in protocol 2 chip units (L2 owns formatting; the driver translated the
    // physical settings, see configSet* above; all zero when the driver has no such units)
    struct MotorConfigSnapshot {
        uint8_t kvalHold, kvalRun, kvalAcc, kvalDec;
        uint8_t driveMode;
        uint8_t tvalHold, tvalRun, tvalAcc, tvalDec;
        uint8_t cmPredEn, cmTsw, cmTFast, cmTonMin, cmToffMin;   // resolved (defaults filled in)
        uint16_t idleReleaseMs;
        uint8_t ocdThreshold, stallThreshold;
        uint16_t acceleration, deceleration, maxSpeed, minSpeed, fsSpeed;
        bool faultOcd, faultThermalSD, faultThermalWarn, faultUvlo;
        bool faultStallA, faultStallB, faultCmdErr;
        uint8_t faultAction;
        uint8_t stepMode;       // log2 of the microsteps asked for
        bool valid;
    };
    virtual MotorConfigSnapshot getMotorConfig() = 0;
};

} // namespace Services
