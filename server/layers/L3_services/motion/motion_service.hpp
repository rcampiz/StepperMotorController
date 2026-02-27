/**
 * @file motion_service.hpp
 * @brief Motion service — thin wrappers around MotorTask_SendCommand
 *
 * Namespace free functions (no class, no vtable). Parser calls these
 * instead of constructing MotorCommand structs directly.
 */

#pragma once
#include <stdint.h>

namespace Services::Motion {

enum class Result : uint8_t { OK, QUEUE_FULL, INVALID_PARAM, FAULT };

const char *resultToString(Result r);

Result run(uint32_t stepsPerSec, bool forward);
Result move(int32_t steps, uint32_t maxSpeedStepsPerSec = 0);
Result goTo(int32_t position, uint32_t maxSpeedStepsPerSec = 0);
Result stop(bool hard);
Result enable();
Result disable();
Result home(uint32_t maxSpeedStepsPerSec = 0);
Result zero();

} // namespace Services::Motion
