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

/// Which diagnostic text a driver produces (driver boundary step 3)
enum class DriverDiagView : uint8_t {
    NONE = 0,       ///< decoded fields only (fault checks)
    REGISTERS,      ///< the driver's register dump, several lines (DBG:MOTOR)
    SUMMARY         ///< one line of what the last apply left on the chip (DRV:CFG:APPLY)
};

struct MotorDebugParams {
    uint16_t status;      ///< the chip's status word (opaque; decoded fields below)
    uint8_t  faults;      ///< Harness::DriverFault bits, decoded by the driver
    bool     outputsOff;
    bool     moving;
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
    uint8_t  adcOut;      ///< ADC_OUT (5 bits): the ADCIN pin (not a supply divider on the IHM03A1)
    uint8_t  tFast;       ///< T_FAST register (current mode: fast decay timing)
};

} // namespace Harness
