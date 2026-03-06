/**
 * @file config_service.hpp
 * @brief Configuration service — ACC/DEC/MAXSPD with integer unit conversion
 *
 * SCPI commands use physical units (steps/s, steps/s^2).
 * All conversions are integer math (no floats).
 *
 * Conversion formulas (powerSTEP01):
 *   ACC/DEC: raw = stepsPerSecSq * 1000 / 14552
 *            stepsPerSecSq = raw * 14552 / 1000
 *   MAX_SPEED: raw = stepsPerSec * 1024 / 15625
 *              stepsPerSec = raw * 15625 / 1024
 */

#pragma once
#include <stdint.h>

namespace Services::Config {

enum class Result : uint8_t { OK, QUEUE_FULL, INVALID_PARAM };

const char *resultToString(Result r);

// Physical-unit setters (steps/s^2, steps/s)
Result setAccelPhysical(uint32_t stepsPerSecSq);
Result setDecelPhysical(uint32_t stepsPerSecSq);
Result setMaxSpeedPhysical(uint32_t stepsPerSec);

// Unit conversion helpers (exposed for response formatting)
uint32_t accelRawToPhysical(uint16_t raw);
uint16_t accelPhysicalToRaw(uint32_t stepsPerSecSq);
uint32_t maxSpeedRawToPhysical(uint16_t raw);
uint16_t maxSpeedPhysicalToRaw(uint32_t stepsPerSec);

} // namespace Services::Config
