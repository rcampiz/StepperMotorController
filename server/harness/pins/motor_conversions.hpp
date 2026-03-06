/**
 * @file motor_conversions.hpp
 * @brief Pure-math motor register conversions — boundary contract
 *
 * Inline functions for converting raw powerSTEP01 register values
 * to display units. Used by SystemSnapshot (getSnapshot) and
 * motor_controller (local control loop).
 */

#pragma once

#include <stdint.h>

namespace Harness {

/**
 * @brief Convert raw SPEED register (20-bit) to steps/s
 * Formula: steps/s = raw * 15625 / 1048576
 */
inline uint32_t rawSpeedToStepsPerSec(uint32_t rawSpeed)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(rawSpeed) * 15625ULL) / 1048576ULL);
}

/**
 * @brief Sign-extend 22-bit ABS_POS register to int32_t
 */
inline int32_t signExtendAbsPos(uint32_t rawPos)
{
    if ((rawPos & 0x200000) != 0) {
        return static_cast<int32_t>(rawPos | 0xFFC00000U);
    }
    return static_cast<int32_t>(rawPos);
}

} // namespace Harness
