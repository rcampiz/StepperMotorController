/**
 * @file imotor_driver.hpp
 * @brief Interface for motor driver hardware abstraction
 *
 * Foundation interface for motor driver hardware abstraction.
 * Implemented directly by Drivers::PowerSTEP01. Enables mock
 * injection for unit testing motor_task logic without real hardware.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "harness/pins/motor_state.hpp"
#include "harness/pins/driver_settings.hpp"
#include "harness/pins/motor_debug_params.hpp"

namespace Drivers {

/**
 * @brief The driver's state, decoded by the driver (chip-neutral; see harness/pins/motor_state.hpp).
 *        Each chip's status word and bit layout stay inside its L4 driver.
 */
using MotorStatus = Harness::MotorState;
using Harness::DriverCaps;
using Harness::DriverSettings;
using Harness::DriveLevels;
using Harness::DriveMode;
using Harness::ApplyResult;

/**
 * @brief Protocol 2's powerSTEP01 chip units, kept for one release so the old commands
 *        (DRV:CFG:KVAL/TVAL/OCD/STALL/MOTION/STEPMODE/PRED/DECAY/FAULT, DRV:CFG?) and the
 *        V1-V4 config blobs keep working (docs/design/DRIVER_BOUNDARY.md).
 *
 * Only a driver can translate them (IMotorDriver::toLegacyUnits / fromLegacyUnits); a driver
 * for another chip answers false and the aliases report an error. Removed with protocol 3.
 */
struct LegacyDriverUnits {
    uint8_t  kval[4];       ///< voltage-mode duty codes: hold, run, acc, dec
    uint8_t  tval[4];       ///< current-mode reference codes: hold, run, acc, dec
    uint8_t  driveMode;     ///< 0 voltage, 1 current
    uint8_t  stepMode;      ///< log2 of the microsteps asked for (0 full step .. 7 = 1/128)
    uint8_t  ocdTh;         ///< overcurrent threshold code
    uint8_t  stallTh;       ///< stall threshold code
    uint16_t acc;           ///< acceleration code
    uint16_t dec;           ///< deceleration code
    uint16_t maxSpeed;      ///< speed ceiling code
    uint16_t minSpeed;      ///< minimum speed code
    uint16_t fsSpd;         ///< full-step switch speed code
    uint8_t  predEn;        ///< current regulation: predictive control 0/1
    uint8_t  tsw;           ///< target switching period code 0..31 (0xFF in: chip default)
    uint8_t  tFast;         ///< fast decay timing code (0 in: chip default)
    uint8_t  tonMin;        ///< minimum on time code (0 in: chip default)
    uint8_t  toffMin;       ///< minimum off time code (0 in: chip default)
    uint8_t  alarms;        ///< bit 0 OCD, 1 TH_SD, 2 TH_WARN, 3 UVLO, 4 STALL_A, 5 STALL_B, 6 CMD_ERR
};

/**
 * @brief Abstract motor driver interface
 *
 * Physical units (docs/design/DRIVER_BOUNDARY.md, steps 2-3): speeds in full steps/s,
 * accelerations in full steps/s^2, currents in amps, positions and distances in microsteps
 * at the resolution in force (floats: Cortex-M4F, task context only, never from an ISR).
 * Each driver converts to its chip's own units and limits and keeps its register map,
 * quirks and recovery to itself (powerSTEP01: L4_drivers/devices/powerstep01_*.hpp).
 * Hot path: readState() every 50 ms in the motor loop.
 * Virtual dispatch ~5ns vs ~50us SPI transaction = negligible overhead.
 */
class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;

    /** @brief Initialize/reinitialize the driver hardware (the chip's own init-only setup) */
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
    /// The MARK position goMark() goes to, microsteps
    virtual void setMark(int32_t positionMicrosteps) = 0;

    /// Speed ceiling of the motion profiles (MOVE/GOTO/HOME, and RUN), full steps/s. A
    /// positive value never rounds to 0 (the chip's smallest step instead); 0 sets 0.
    /// getMaxSpeed() reads back the value in force, so setMaxSpeed(getMaxSpeed()) restores it
    /// exactly (the per-move override relies on that).
    virtual void setMaxSpeed(float fullStepsPerSec) = 0;
    virtual float getMaxSpeed() = 0;
    /// Profile acceleration / deceleration, full steps/s^2 (a positive value never rounds to 0)
    virtual void setAcceleration(float fullStepsPerSec2) = 0;
    virtual void setDeceleration(float fullStepsPerSec2) = 0;

    // --- Settings (driver boundary step 3) ---
    /// What this driver can do. Pure: no bus traffic, any task.
    virtual const DriverCaps& caps() const = 0;
    /// The settings as apply() would put them in force (resolution capped, every value on the
    /// chip's grid). Pure: no bus traffic, any task. quantize(quantize(s)) == quantize(s).
    virtual DriverSettings quantize(const DriverSettings& s) const = 0;
    /// Put the settings in force. Settings the chip accepts only with the outputs off are
    /// written when they already are off; otherwise NEEDS_OUTPUTS_OFF (the rest is in force)
    /// and the caller decides when to release the outputs and apply again. Clears latched faults.
    virtual ApplyResult apply(const DriverSettings& s) = 0;
    /// Read back what is in force on the chip (resolution and drive mode always; the levels of
    /// the active drive mode; tuning not decoded). False when the chip cannot be read.
    virtual bool readSettings(DriverSettings& inForce) = 0;
    /// The chip lost its configuration behind the firmware's back (supply drop): call
    /// periodically (task context); true = re-initialise and apply the settings again.
    virtual bool checkReset() { return false; }
    /// The run current reference in force now, amps (current mode): the chip's run level
    /// alone, written at once, also while running; the stored settings are not changed (the
    /// next apply() writes them again). False when the drive cannot (voltage mode, no such
    /// chip): the caller stops asking. The resonance avoider's actuator.
    virtual bool setRunCurrentLive(float amps) { (void)amps; return false; }
    /// The driver's own diagnostic text for a view (opaque above L4): the length written.
    virtual size_t diagnostics(Harness::DriverDiagView view, char* buf, size_t size)
    {
        (void)view;
        if (buf != nullptr && size != 0) buf[0] = '\0';
        return 0;
    }

    // --- Protocol 2 chip units (one release, see LegacyDriverUnits). Pure, any task. ---
    virtual bool toLegacyUnits(const DriverSettings& s, LegacyDriverUnits& out) const
    {
        (void)s; (void)out;
        return false;
    }
    /// Overwrites `s` with the settings the units describe (fields the units do not cover kept)
    virtual bool fromLegacyUnits(const LegacyDriverUnits& in, DriverSettings& s) const
    {
        (void)in; (void)s;
        return false;
    }

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
