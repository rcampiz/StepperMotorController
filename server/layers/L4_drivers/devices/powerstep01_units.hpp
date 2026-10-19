/**
 * @file powerstep01_units.hpp
 * @brief powerSTEP01 register units <-> physical units (datasheet DS9836).
 *
 * The only place that knows the chip's speed and position encodings (the driver and the
 * virtual Nucleo's fake chip both use it). Above L4 everything is physical: speeds in full
 * steps/s, positions in microsteps (docs/design/DRIVER_BOUNDARY.md, step 2).
 *
 *   SPEED      20-bit, 2^-28 step per 250 ns tick: 1 LSB = 15625 / 2^20 full steps/s
 *              (67.108864 LSB per full step/s); max 0xFFFFF = 15624.985 full steps/s
 *   MAX_SPEED  10-bit, 2^-18 step per tick:        1 LSB = 15625 / 2^10 full steps/s
 *              (15.2587890625 full steps/s per LSB); max 0x3FF = 15610 full steps/s
 *   ABS_POS    22-bit two's complement, microsteps
 *
 * Conversions to the chip truncate (as the integer code above L4 always did), and are exact
 * for whole steps/s: fspsToSpeedReg(n) == n * 2^20 / 15625 (integer division). Floats are
 * fine here (Cortex-M4F, task context); none of this runs in an ISR.
 */
#pragma once

#include <stdint.h>

namespace Drivers {
namespace PowerStepUnits {

constexpr uint32_t SPEED_REG_MAX = 0xFFFFFu;        ///< 20-bit SPEED / RUN argument
constexpr uint32_t MAX_SPEED_REG_MAX = 0x3FFu;      ///< 10-bit MAX_SPEED
constexpr uint32_t ABS_POS_MASK = 0x3FFFFFu;        ///< 22-bit ABS_POS

/// Full steps/s -> SPEED register (RUN argument): floor(fsps x 2^20 / 15625), clamped to
/// 0..0xFFFFF. Zero, negative or NaN -> 0. (2^20, what 15625 steps/s would encode to, does
/// not fit the 20-bit field: the request saturates at the chip's top speed.)
inline uint32_t fspsToSpeedReg(float fsps)
{
    if (!(fsps > 0.0f)) return 0;
    if (fsps >= 15625.0f) return SPEED_REG_MAX;
    // fsps x 2^20 is exact in float (power-of-two scaling); floor(floor(z) / m) == floor(z / m)
    const uint64_t q20 = static_cast<uint64_t>(fsps * 1048576.0f);
    const uint64_t reg = q20 / 15625u;
    return reg > SPEED_REG_MAX ? SPEED_REG_MAX : static_cast<uint32_t>(reg);
}

/// SPEED register -> full steps/s (magnitude). The integer part is exact (the float is the
/// largest one not above reg x 15625 / 2^20 within a float step), so truncating it gives
/// floor(reg x 15625 / 2^20), what the telemetry has always reported.
inline float speedRegToFsps(uint32_t reg)
{
    const uint64_t n = static_cast<uint64_t>(reg & SPEED_REG_MAX) * 15625u;   // fsps x 2^20
    const uint32_t whole = static_cast<uint32_t>(n >> 20);
    const uint32_t frac = static_cast<uint32_t>(n & 0xFFFFFu);
    float f = static_cast<float>(whole) + static_cast<float>(frac) * (1.0f / 1048576.0f);
    const float next = static_cast<float>(whole + 1u);
    if (f >= next) {
        f = next * (1.0f - 1.0f / 16777216.0f);   // the float just below the next integer
    }
    return f;
}

/// Full steps/s -> MAX_SPEED register: floor(fsps x 1024 / 15625), clamped to 0..0x3FF.
/// A positive request never becomes 0 (that would stop every profile move): it gets at
/// least 1 LSB (15.26 full steps/s), as the per-move override always did. 0 -> 0.
inline uint32_t fspsToMaxSpeedReg(float fsps)
{
    if (!(fsps > 0.0f)) return 0;
    if (fsps >= 15625.0f) return MAX_SPEED_REG_MAX;
    const uint32_t reg = static_cast<uint32_t>(fsps * 1024.0f) / 15625u;   // fsps x 1024 exact
    if (reg < 1u) return 1u;
    return reg > MAX_SPEED_REG_MAX ? MAX_SPEED_REG_MAX : reg;
}

/// MAX_SPEED register -> full steps/s: reg x 15625 / 1024, exact in float, so
/// fspsToMaxSpeedReg(maxSpeedRegToFsps(r)) == r for every register value.
inline float maxSpeedRegToFsps(uint32_t reg)
{
    return static_cast<float>(reg & MAX_SPEED_REG_MAX) * (15625.0f / 1024.0f);
}

/// ABS_POS register (22-bit two's complement) -> signed microsteps
inline int32_t absPosToUsteps(uint32_t reg)
{
    reg &= ABS_POS_MASK;
    if ((reg & 0x200000u) != 0u) {
        return static_cast<int32_t>(reg | 0xFFC00000u);
    }
    return static_cast<int32_t>(reg);
}

} // namespace PowerStepUnits
} // namespace Drivers
