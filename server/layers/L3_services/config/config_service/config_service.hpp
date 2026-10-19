/**
 * @file config_service.hpp
 * @brief Configuration service — live profile acceleration, deceleration and speed ceiling
 *
 * SCPI commands (MOT:CFG:ACCEL/DECEL/MAXSPD) use physical units: full steps/s^2 and full
 * steps/s. The values reach the driver as they are (MotorCommand); the driver converts to its
 * chip. The accepted ranges are the driver's (DriverCaps), not constants here.
 */

#pragma once
#include <stdint.h>

namespace Services::Config {

enum class Result : uint8_t { OK, QUEUE_FULL, INVALID_PARAM };

const char *resultToString(Result r);

// Physical-unit setters (full steps/s^2, full steps/s): live, until the next APPLY / reinit
Result setAccelPhysical(uint32_t stepsPerSecSq);
Result setDecelPhysical(uint32_t stepsPerSecSq);
Result setMaxSpeedPhysical(uint32_t stepsPerSec);

} // namespace Services::Config
