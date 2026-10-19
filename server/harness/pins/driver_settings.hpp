/**
 * @file driver_settings.hpp
 * @brief The motor driver's settings and capabilities, in physical units (shared POD pin).
 *
 * Driver boundary step 3 (docs/design/DRIVER_BOUNDARY.md). Above L4 a driver is what its
 * DriverCaps say it can do, configured by a DriverSettings in physical units; each L4 driver
 * maps them onto its chip (registers, units, quirks such as the writes that need the outputs
 * off). Nothing above L4 knows a register.
 *
 * Units: currents in amps, drive duty as a fraction of the supply (0..1), speeds in full
 * steps/s, accelerations in full steps/s^2, resolution in microsteps per full step.
 * Floats: Cortex-M4F, task context only (never in an ISR).
 *
 * DriverSettings is persisted as-is in the motor config blob (V5): its layout is a storage
 * format. Append fields only at the end of DriverTuning's reserve, never reorder.
 */
#pragma once

#include <stdint.h>
#include "harness/pins/motor_state.hpp"

namespace Harness {

/// How the coils are driven: a current reference (CURRENT) or a PWM duty of the supply (VOLTAGE)
enum class DriveMode : uint8_t { VOLTAGE = 0, CURRENT = 1 };

/// What a driver can do. Pure data, constant for a driver: L3 validates and adapts to it, the
/// LCD offers only what exists, protocol limits come from it.
struct DriverCaps {
    // --- Drive modes and resolution ---
    bool     voltageControl;          ///< DriveMode::VOLTAGE exists (DriveLevels as duty 0..1)
    bool     currentControl;          ///< DriveMode::CURRENT exists (the chip regulates coil current)
    bool     currentSettable;         ///< its reference is set by DriverSettings::currentA (false:
                                      ///< set in hardware, e.g. a VREF pot; the amps read 0)
    uint16_t maxMicrostepsVoltage;    ///< finest resolution in voltage mode (power of two; 0: no such mode)
    uint16_t maxMicrostepsCurrent;    ///< finest resolution in current mode (power of two; 0: no such mode)

    // --- Features ---
    bool     stallDetect;             ///< DriverSettings::stallThreshold01 means something
    bool     standbyPin;              ///< setStandby() drives a real line (the out-of-band E-STOP)
    bool     hardwarePositioning;     ///< move/goTo/goHome/goMark run as profiles in the chip
    uint8_t  syncPulsesPerFullStep;   ///< pulses on the step-sync output per full step (0: none)
    uint8_t  faultsReported;          ///< DriverFault bits the driver can report
    uint8_t  faultsAlarmable;         ///< DriverFault bits DriverSettings::faultAlarms can arm

    // --- Currents and protection ---
    float    maxCurrentA;             ///< largest current reference (current mode)
    float    currentStepA;            ///< current reference resolution
    float    dutyStep;                ///< duty resolution (voltage mode)
    float    overcurrentMinA;         ///< overcurrent threshold range and resolution
    float    overcurrentMaxA;
    float    overcurrentStepA;
    float    overcurrentOhms;         ///< > 0: the chip senses a VOLTAGE across a switch of about this
                                      ///< resistance (an estimate); the amps above are volts / ohms,
                                      ///< estimates too, and a display shows both. 0: sensed as current.
    float    stallStep01;            ///< stall threshold resolution on its 0..1 scale

    // --- Motion ---
    float    maxSpeedFsps;            ///< fastest speed a run may ask for (faster requests saturate)
    float    maxSpeedLimitFsps;       ///< largest profile speed ceiling (DriverSettings::maxSpeedFsps)
    float    speedLimitStepFsps;      ///< resolution of that ceiling
    float    accelMinFsps2;           ///< acceleration/deceleration range and resolution
    float    accelMaxFsps2;
    float    accelStepFsps2;
    int32_t  minPositionUsteps;       ///< position range the driver can address (microsteps)
    int32_t  maxPositionUsteps;

    /// The finest resolution the drive mode offers (1 when the mode does not exist)
    uint16_t maxMicrosteps(DriveMode m) const
    {
        const uint16_t n = (m == DriveMode::CURRENT) ? maxMicrostepsCurrent : maxMicrostepsVoltage;
        return n != 0 ? n : 1;
    }
    /// The finest resolution in any mode (protocol limits)
    uint16_t maxMicrostepsAny() const
    {
        return maxMicrostepsCurrent > maxMicrostepsVoltage ? maxMicrostepsCurrent : maxMicrostepsVoltage;
    }
    bool hasMode(DriveMode m) const { return (m == DriveMode::CURRENT) ? currentControl : voltageControl; }
};

/// Hold / run / accelerate / decelerate drive levels: amps (current mode) or duty 0..1 (voltage)
struct DriveLevels {
    float hold;
    float run;
    float acc;
    float dec;
};

/// Chip-specific fine tuning, owned by the driver (powerSTEP01: current-regulation timing,
/// minimum and full-step switch speeds, which bridges report a stall). Opaque above L4: stored and handed back
/// as-is. A driver treats a block tagged for another chip, or tag 0, as "all defaults".
struct DriverTuning {
    uint8_t tag;          ///< the owning driver's tag; 0 = defaults
    uint8_t data[11];
};

/// Everything a driver is configured with, in physical units. Persisted as-is (config V5).
struct DriverSettings {
    uint16_t     microsteps;        ///< resolution asked for (microsteps per full step, power of two);
                                    ///< the driver caps it to what the drive mode offers
    DriveMode    mode;              ///< current or voltage drive (as DriverCaps allow)
    uint8_t      faultAlarms;       ///< DriverFault bits that raise the driver's alarm output
    DriveLevels  currentA;          ///< current mode: coil current references, amps
    DriveLevels  duty;              ///< voltage mode: PWM duty of the supply, 0..1
    float        accelFsps2;        ///< profile acceleration, full steps/s^2
    float        decelFsps2;        ///< profile deceleration, full steps/s^2
    float        maxSpeedFsps;      ///< profile speed ceiling (MOVE/GOTO/HOME and RUN), full steps/s
    float        overcurrentA;      ///< overcurrent detection threshold, amps (an estimate when
                                    ///< DriverCaps::overcurrentOhms > 0: the chip senses a voltage)
    float        stallThreshold01;  ///< stall threshold on the driver's scale: 0 = most sensitive, 1 = least
    DriverTuning tuning;            ///< chip-specific, opaque (see DriverTuning)
};

static_assert(sizeof(DriverSettings) == 68, "DriverSettings is a storage format (config blob V5)");

/// What DriverSettings::apply-style calls report
enum class ApplyResult : uint8_t {
    APPLIED = 0,          ///< every setting is in force
    NEEDS_OUTPUTS_OFF,    ///< some settings take effect only with the outputs off (released):
                          ///< the rest are in force; apply again with the outputs off
    VERIFY_FAILED         ///< written with the outputs off, but the driver reads back something else
};

/// The resolution in force for a request: the requested microsteps rounded down to a power of
/// two and capped at what the drive mode offers (the caps say how far each mode goes)
inline uint16_t effectiveMicrosteps(const DriverCaps& caps, uint16_t requested, DriveMode mode)
{
    const uint16_t cap = caps.maxMicrosteps(mode);
    uint16_t n = requested != 0 ? requested : 1;
    if (n > cap) n = cap;
    uint16_t p = 1;
    while (static_cast<uint32_t>(p) * 2u <= n) p = static_cast<uint16_t>(p * 2u);
    return p;
}

/// log2 of a power-of-two microstep count (the protocol's "step mode" 0 = full step, 1 = 1/2...)
inline uint8_t microstepsLog2(uint16_t microsteps)
{
    uint8_t k = 0;
    while (k < 15u && (1u << (k + 1u)) <= microsteps) ++k;
    return k;
}

// --- What a host may ask for (driver boundary step 4): the protocol's ranges and L3's check ---
// A value within half a resolution step of a range end is accepted (the driver rounds it onto
// its grid, quantize() says where). NaN is never within a range.

inline bool withinRange(float v, float lo, float hi, float step)
{
    const float slack = (step > 0.0f) ? step * 0.5f : 0.0f;
    return v >= lo - slack && v <= hi + slack;
}

/// Current reference, amps (current mode): 0..maxCurrentA
inline bool currentWithinCaps(const DriverCaps& c, float a)
{
    return c.currentControl && c.currentSettable && withinRange(a, 0.0f, c.maxCurrentA, c.currentStepA);
}
/// Drive duty (voltage mode): 0..1
inline bool dutyWithinCaps(const DriverCaps& c, float d)
{
    return c.voltageControl && withinRange(d, 0.0f, 1.0f, 0.0f);
}
/// Profile acceleration / deceleration, full steps/s^2
inline bool accelWithinCaps(const DriverCaps& c, float a)
{
    return a > 0.0f && withinRange(a, c.accelMinFsps2, c.accelMaxFsps2, c.accelStepFsps2);
}
/// Profile speed ceiling, full steps/s (one resolution step up to maxSpeedLimitFsps)
inline bool speedLimitWithinCaps(const DriverCaps& c, float f)
{
    return f > 0.0f && withinRange(f, c.speedLimitStepFsps, c.maxSpeedLimitFsps, c.speedLimitStepFsps);
}
/// Overcurrent threshold, amps (an estimate when overcurrentOhms > 0)
inline bool overcurrentWithinCaps(const DriverCaps& c, float a)
{
    return withinRange(a, c.overcurrentMinA, c.overcurrentMaxA, c.overcurrentStepA);
}
/// Stall threshold on the driver's 0..1 scale (drivers with stall detection)
inline bool stallWithinCaps(const DriverCaps& c, float x)
{
    return c.stallDetect && withinRange(x, 0.0f, 1.0f, 0.0f);
}
/// Microsteps asked for: a power of two from 1 to the finest resolution of any drive mode
inline bool microstepsWithinCaps(const DriverCaps& c, uint32_t n)
{
    return n >= 1u && n <= c.maxMicrostepsAny() && (n & (n - 1u)) == 0u;
}

} // namespace Harness
