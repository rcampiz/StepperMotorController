/**
 * @file unit_conversion.cpp
 * @brief Physical unit conversion — pure integer math
 *
 * No RTOS, no hardware, no heap, no floats.
 * All fractional conversions use X1000 scaling for 3-decimal precision.
 */

#include "services/unit_conversion.hpp"
#include <string.h>

namespace Services::Units {

// --- Position conversions ---

int32_t toMicrosteps(int32_t value, PositionUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case PositionUnit::REV:
            return value * static_cast<int32_t>(ustepsPerRev);
        case PositionUnit::DEG:
            // usteps = value * ustepsPerRev / 360
            return static_cast<int32_t>(
                (static_cast<int64_t>(value) * ustepsPerRev) / 360);
        case PositionUnit::STEPS:
        default:
            return value;
    }
}

int32_t toMicrostepsX1000(int32_t valueX1000, PositionUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case PositionUnit::REV:
            // usteps = (valueX1000 * ustepsPerRev) / 1000
            return static_cast<int32_t>(
                (static_cast<int64_t>(valueX1000) * ustepsPerRev) / 1000);
        case PositionUnit::DEG:
            // usteps = (valueX1000 * ustepsPerRev) / (360 * 1000)
            return static_cast<int32_t>(
                (static_cast<int64_t>(valueX1000) * ustepsPerRev) / 360000);
        case PositionUnit::STEPS:
        default:
            return valueX1000 / 1000;
    }
}

int32_t fromMicrostepsX1000(int32_t usteps, PositionUnit unit, uint32_t ustepsPerRev)
{
    if (ustepsPerRev == 0) return 0;
    switch (unit) {
        case PositionUnit::REV:
            return static_cast<int32_t>(
                (static_cast<int64_t>(usteps) * 1000) / static_cast<int32_t>(ustepsPerRev));
        case PositionUnit::DEG:
            return static_cast<int32_t>(
                (static_cast<int64_t>(usteps) * 360000) / static_cast<int32_t>(ustepsPerRev));
        case PositionUnit::STEPS:
        default:
            return usteps * 1000;
    }
}

// --- Speed conversions ---

uint32_t toStepsPerSec(uint32_t value, SpeedUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case SpeedUnit::REV_PER_SEC:
            return value * ustepsPerRev;
        case SpeedUnit::RPM:
            // steps/s = RPM * ustepsPerRev / 60
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(value) * ustepsPerRev) / 60);
        case SpeedUnit::STEPS_PER_SEC:
        default:
            return value;
    }
}

uint32_t toStepsPerSecX1000(uint32_t valueX1000, SpeedUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case SpeedUnit::REV_PER_SEC:
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(valueX1000) * ustepsPerRev) / 1000);
        case SpeedUnit::RPM:
            // steps/s = (RPM_x1000 * ustepsPerRev) / (60 * 1000)
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(valueX1000) * ustepsPerRev) / 60000);
        case SpeedUnit::STEPS_PER_SEC:
        default:
            return valueX1000 / 1000;
    }
}

uint32_t fromStepsPerSecX1000(uint32_t sps, SpeedUnit unit, uint32_t ustepsPerRev)
{
    if (ustepsPerRev == 0) return 0;
    switch (unit) {
        case SpeedUnit::REV_PER_SEC:
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(sps) * 1000) / ustepsPerRev);
        case SpeedUnit::RPM:
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(sps) * 60000) / ustepsPerRev);
        case SpeedUnit::STEPS_PER_SEC:
        default:
            return sps * 1000;
    }
}

// --- Acceleration conversions ---

uint32_t toStepsPerSecSq(uint32_t value, AccelUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case AccelUnit::REV_PER_SEC_SQ:
            return value * ustepsPerRev;
        case AccelUnit::STEPS_PER_SEC_SQ:
        default:
            return value;
    }
}

uint32_t toStepsPerSecSqX1000(uint32_t valueX1000, AccelUnit unit, uint32_t ustepsPerRev)
{
    switch (unit) {
        case AccelUnit::REV_PER_SEC_SQ:
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(valueX1000) * ustepsPerRev) / 1000);
        case AccelUnit::STEPS_PER_SEC_SQ:
        default:
            return valueX1000 / 1000;
    }
}

// --- Unit string parsing ---

static int strcasecmpHelper(const char* a, const char* b)
{
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (*b - 32) : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

bool parsePositionUnit(const char* token, PositionUnit* unit)
{
    if (!token || !unit) return false;

    if (strcasecmpHelper(token, "REV") == 0) {
        *unit = PositionUnit::REV;
        return true;
    }
    if (strcasecmpHelper(token, "DEG") == 0) {
        *unit = PositionUnit::DEG;
        return true;
    }
    if (strcasecmpHelper(token, "STEPS") == 0 ||
        strcasecmpHelper(token, "STEP") == 0) {
        *unit = PositionUnit::STEPS;
        return true;
    }
    return false;
}

bool parseSpeedUnit(const char* token, SpeedUnit* unit)
{
    if (!token || !unit) return false;

    if (strcasecmpHelper(token, "RPM") == 0) {
        *unit = SpeedUnit::RPM;
        return true;
    }
    if (strcasecmpHelper(token, "REV/S") == 0 ||
        strcasecmpHelper(token, "REVS") == 0) {
        *unit = SpeedUnit::REV_PER_SEC;
        return true;
    }
    if (strcasecmpHelper(token, "STEPS/S") == 0 ||
        strcasecmpHelper(token, "SPS") == 0) {
        *unit = SpeedUnit::STEPS_PER_SEC;
        return true;
    }
    return false;
}

bool parseAccelUnit(const char* token, AccelUnit* unit)
{
    if (!token || !unit) return false;

    if (strcasecmpHelper(token, "REV/S2") == 0 ||
        strcasecmpHelper(token, "REVS2") == 0) {
        *unit = AccelUnit::REV_PER_SEC_SQ;
        return true;
    }
    if (strcasecmpHelper(token, "STEPS/S2") == 0 ||
        strcasecmpHelper(token, "SPS2") == 0) {
        *unit = AccelUnit::STEPS_PER_SEC_SQ;
        return true;
    }
    return false;
}

const char* positionUnitName(PositionUnit unit)
{
    switch (unit) {
        case PositionUnit::REV:   return "rev";
        case PositionUnit::DEG:   return "deg";
        case PositionUnit::STEPS: return "steps";
    }
    return "?";
}

const char* speedUnitName(SpeedUnit unit)
{
    switch (unit) {
        case SpeedUnit::RPM:           return "RPM";
        case SpeedUnit::REV_PER_SEC:   return "rev/s";
        case SpeedUnit::STEPS_PER_SEC: return "steps/s";
    }
    return "?";
}

} // namespace Services::Units
