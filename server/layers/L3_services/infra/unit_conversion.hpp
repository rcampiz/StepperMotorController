/**
 * @file unit_conversion.hpp
 * @brief Physical unit conversion for motion commands and telemetry
 *
 * Pure math — no RTOS, no hardware, no heap.
 * Converts between physical units (revolutions, degrees, RPM) and
 * internal units (microsteps, microsteps/s).
 *
 * All conversions require microsteps_per_rev as parameter, derived from:
 *   microsteps_per_rev = full_steps_per_rev * (1 << step_mode)
 */

#pragma once
#include <stdint.h>

namespace Services::Units {

enum class PositionUnit : uint8_t {
    STEPS = 0,    // Microsteps (internal unit)
    REV   = 1,    // Revolutions
    DEG   = 2     // Degrees
};

enum class SpeedUnit : uint8_t {
    STEPS_PER_SEC = 0,  // Microsteps/second (internal unit)
    REV_PER_SEC   = 1,  // Revolutions/second
    RPM           = 2   // Revolutions/minute
};

enum class AccelUnit : uint8_t {
    STEPS_PER_SEC_SQ = 0,  // Microsteps/s² (internal unit)
    REV_PER_SEC_SQ   = 1   // Revolutions/s²
};

// --- Position conversions ---

// Physical → microsteps
int32_t toMicrosteps(int32_t value, PositionUnit unit, uint32_t ustepsPerRev);

// Fractional physical → microsteps (for rev/deg with decimal input)
// valueX1000 = value * 1000 (e.g., 2.5 rev → 2500)
int32_t toMicrostepsX1000(int32_t valueX1000, PositionUnit unit, uint32_t ustepsPerRev);

// Microsteps → physical (returns value * 1000 for precision)
int32_t fromMicrostepsX1000(int32_t usteps, PositionUnit unit, uint32_t ustepsPerRev);

// --- Speed conversions ---

// Physical → microsteps/s
uint32_t toStepsPerSec(uint32_t value, SpeedUnit unit, uint32_t ustepsPerRev);

// Fractional physical → microsteps/s
// valueX1000 = value * 1000 (e.g., 2.5 rev/s → 2500)
uint32_t toStepsPerSecX1000(uint32_t valueX1000, SpeedUnit unit, uint32_t ustepsPerRev);

// Microsteps/s → physical (returns value * 1000 for precision)
uint32_t fromStepsPerSecX1000(uint32_t sps, SpeedUnit unit, uint32_t ustepsPerRev);

// --- Acceleration conversions ---

uint32_t toStepsPerSecSq(uint32_t value, AccelUnit unit, uint32_t ustepsPerRev);
uint32_t toStepsPerSecSqX1000(uint32_t valueX1000, AccelUnit unit, uint32_t ustepsPerRev);

// --- Unit string parsing ---

// Parse a unit token from command args. Returns true if recognized.
// On success, sets *unit to the parsed value.
// Unrecognized tokens return false (caller should use default).
bool parsePositionUnit(const char* token, PositionUnit* unit);
bool parseSpeedUnit(const char* token, SpeedUnit* unit);
bool parseAccelUnit(const char* token, AccelUnit* unit);

// Unit name strings (for response formatting)
const char* positionUnitName(PositionUnit unit);
const char* speedUnitName(SpeedUnit unit);

} // namespace Services::Units
