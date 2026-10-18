/**
 * @file motor_debug_params.hpp
 * @brief Motor debug register snapshot — shared boundary type
 *
 * Standalone POD struct for powerSTEP01 register readback.
 * Used by IMotorControlService (task→service) and
 * IMotorDriverDispatcher (service→protocol).
 */

#pragma once

#include <stdint.h>

namespace Harness {

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
    uint8_t  driveMode;   ///< STEP_MODE bit 3 (CM_VM): 0 voltage mode, 1 current mode
    uint8_t  adcOut;      ///< ADC_OUT (5 bits): the ADCIN pin, a divider on the motor supply
};

} // namespace Harness
