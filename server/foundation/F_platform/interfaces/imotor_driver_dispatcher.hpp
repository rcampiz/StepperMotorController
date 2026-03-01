/**
 * @file imotor_driver_dispatcher.hpp
 * @brief Interface for motor driver management (MOTOR_REINIT, DRV namespace)
 */

#pragma once

#include <stdint.h>

namespace Comms {

class IMotorDriverDispatcher {
public:
    virtual ~IMotorDriverDispatcher() = default;

    /** @brief POD mirror of Tasks::MotorDebugInfo (no task header dependency) */
    struct MotorDebugParams {
        uint16_t status;
        uint8_t  kvalHold;
        uint8_t  kvalRun;
        uint8_t  kvalAcc;
        uint8_t  kvalDec;
        uint16_t accel;
        uint16_t decel;
        uint16_t maxSpeed;
        int32_t  absPos;
        uint8_t  ocdTh;
        uint8_t  stallTh;
        uint16_t config;
        uint8_t  alarmEn;
        uint16_t fsSpd;
        uint8_t  stepMode;
    };

    virtual void motorReinit() = 0;
    virtual bool motorApplyConfig() = 0;
    virtual bool motorGetDebugInfo(MotorDebugParams& out) = 0;
    virtual bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) = 0;

    virtual void getFaultEnable(bool& ocd, bool& thermalSD, bool& thermalWarn,
                                bool& uvlo, bool& stallA, bool& stallB,
                                bool& cmdErr) = 0;
};

} // namespace Comms
