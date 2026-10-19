/**
 * @file imotor_driver.hpp
 * @brief Interface for motor driver hardware abstraction
 *
 * Foundation interface for motor driver hardware abstraction.
 * Implemented directly by Drivers::PowerSTEP01. Enables mock
 * injection for unit testing motor_task logic without real hardware.
 */

#pragma once

#include <stdint.h>
#include "harness/pins/motor_state.hpp"

namespace Drivers {

/**
 * @brief The driver's state, decoded by the driver (chip-neutral; see harness/pins/motor_state.hpp).
 *        Each chip's status word and bit layout stay inside its L4 driver.
 */
using MotorStatus = Harness::MotorState;

/**
 * @brief Register address constants (match powerSTEP01 addresses)
 */
namespace MotorReg {
    constexpr uint8_t ABS_POS    = 0x01;
    constexpr uint8_t EL_POS     = 0x02;
    constexpr uint8_t MARK       = 0x03;
    constexpr uint8_t SPEED      = 0x04;
    constexpr uint8_t ACC        = 0x05;
    constexpr uint8_t DEC        = 0x06;
    constexpr uint8_t MAX_SPEED  = 0x07;
    constexpr uint8_t MIN_SPEED  = 0x08;
    constexpr uint8_t KVAL_HOLD  = 0x09;
    constexpr uint8_t KVAL_RUN   = 0x0A;
    constexpr uint8_t KVAL_ACC   = 0x0B;
    constexpr uint8_t KVAL_DEC   = 0x0C;
    constexpr uint8_t INT_SPEED  = 0x0D;
    constexpr uint8_t T_FAST     = 0x0E;   // current mode (ST_SLP in voltage mode)
    constexpr uint8_t TON_MIN    = 0x0F;   // current mode (FN_SLP_ACC in voltage mode)
    constexpr uint8_t TOFF_MIN   = 0x10;   // current mode (FN_SLP_DEC in voltage mode)
    constexpr uint8_t ADC_OUT    = 0x12;   // 5-bit reading of the ADCIN pin (read-only)
    constexpr uint8_t OCD_TH     = 0x13;
    constexpr uint8_t STALL_TH   = 0x14;
    constexpr uint8_t FS_SPD     = 0x15;
    constexpr uint8_t STEP_MODE  = 0x16;
    constexpr uint8_t ALARM_EN   = 0x17;
    constexpr uint8_t GATECFG1   = 0x18;   // gate drive: written only by init, 0 after a chip reset
    constexpr uint8_t CONFIG     = 0x1A;
} // namespace MotorReg

/**
 * @brief Abstract motor driver interface
 *
 * Motion speaks physical units (docs/design/DRIVER_BOUNDARY.md, step 2): speeds in full
 * steps/s (float: Cortex-M4F, task context only, never from an ISR), positions and
 * distances in microsteps at the resolution in force. Each driver converts to its chip's
 * own units and limits (powerSTEP01: L4_drivers/devices/powerstep01_units.hpp).
 * Hot path: readState() every 50 ms in the motor loop.
 * Virtual dispatch ~5ns vs ~50us SPI transaction = negligible overhead.
 */
class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;

    /** @brief Initialize/reinitialize the driver hardware */
    virtual void init() = 0;

    // --- Motion commands (physical units) ---
    /// Run at a constant speed, full steps/s (>= 0). The driver clamps to its chip's range
    /// and resolution (powerSTEP01: truncated to 0.0149 full steps/s, at most 15624.98).
    virtual void run(bool forward, float fullStepsPerSec) = 0;
    /// Relative move, microsteps
    virtual void move(bool forward, uint32_t microsteps) = 0;
    /// Absolute move, microsteps
    virtual void goTo(int32_t positionMicrosteps) = 0;
    virtual void softStop() = 0;
    virtual void hardStop() = 0;
    virtual void softHiZ() = 0;
    virtual void hardHiZ() = 0;
    virtual void goHome() = 0;
    virtual void goMark() = 0;
    virtual void resetPos() = 0;

    /// Speed ceiling of the motion profiles (MOVE/GOTO/HOME, and RUN), full steps/s. A
    /// positive value never rounds to 0 (the chip's smallest step instead); 0 sets 0.
    /// getMaxSpeed() reads back the value in force, so setMaxSpeed(getMaxSpeed()) restores it
    /// exactly (the per-move override relies on that).
    virtual void setMaxSpeed(float fullStepsPerSec) = 0;
    virtual float getMaxSpeed() = 0;

    // --- Register access (config and bring-up only; driver boundary step 3 removes it from L3) ---
    virtual uint32_t getParam(uint8_t reg) = 0;
    virtual void setParam(uint8_t reg, uint32_t value) = 0;

    // --- Status ---
    /// The decoded status only (positionUsteps/speedFsps left 0): fault clears, Hi-Z checks
    virtual MotorStatus getStatus() = 0;
    /// The poll: decoded status + position (microsteps, sign-extended) + speed (full steps/s)
    virtual MotorStatus readState() = 0;
    virtual bool isBusy() = 0;
    virtual bool flagActive() = 0;

    /// STBY/RESET line: true = hold the chip in standby (outputs off, registers reset).
    /// One GPIO write, ISR-safe. init() releases it and re-configures the chip.
    virtual void setStandby(bool standby) { (void)standby; }
};

} // namespace Drivers
