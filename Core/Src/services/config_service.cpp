/**
 * @file config_service.cpp
 * @brief Configuration service — ACC/DEC/MAXSPD with integer unit conversion
 */

#include "services/config_service.hpp"
#include "tasks/motor_task.hpp"

namespace Services::Config {

// ACC/DEC register limits (12-bit)
static constexpr uint16_t ACCEL_RAW_MIN = 1;
static constexpr uint16_t ACCEL_RAW_MAX = 4095;

// MAX_SPEED register limits (10-bit)
static constexpr uint16_t MAXSPD_RAW_MIN = 1;
static constexpr uint16_t MAXSPD_RAW_MAX = 1023;

// Physical limit for ACC/DEC: 4095 * 14552 / 1000 = 59,590 steps/s^2
static constexpr uint32_t ACCEL_PHYS_MAX = 59590;

// Physical limit for MAX_SPEED: 1023 * 15625 / 1024 = 15,609 steps/s
static constexpr uint32_t MAXSPD_PHYS_MAX = 15609;

const char *resultToString(Result r) {
    switch (r) {
    case Result::OK:            return "OK";
    case Result::QUEUE_FULL:    return "Queue full";
    case Result::INVALID_PARAM: return "Invalid parameter";
    default:                    return "Unknown";
    }
}

// --- Unit conversion helpers ---

uint32_t accelRawToPhysical(uint16_t raw) {
    return static_cast<uint32_t>(raw) * 14552U / 1000U;
}

uint16_t accelPhysicalToRaw(uint32_t stepsPerSecSq) {
    return static_cast<uint16_t>(stepsPerSecSq * 1000U / 14552U);
}

uint32_t maxSpeedRawToPhysical(uint16_t raw) {
    return static_cast<uint32_t>(raw) * 15625U / 1024U;
}

uint16_t maxSpeedPhysicalToRaw(uint32_t stepsPerSec) {
    return static_cast<uint16_t>(stepsPerSec * 1024U / 15625U);
}

// --- Internal send helper ---

static Result sendParam(Tasks::MotorCmdType type, int32_t value) {
    Tasks::MotorCommand cmd = {};
    cmd.type = type;
    cmd.param1 = value;
    return Tasks::MotorTask_SendCommand(cmd) ? Result::OK : Result::QUEUE_FULL;
}

// --- Physical-unit setters (SCPI commands) ---

Result setAccelPhysical(uint32_t stepsPerSecSq) {
    if (stepsPerSecSq == 0 || stepsPerSecSq > ACCEL_PHYS_MAX) {
        return Result::INVALID_PARAM;
    }
    uint16_t raw = accelPhysicalToRaw(stepsPerSecSq);
    if (raw < ACCEL_RAW_MIN) raw = ACCEL_RAW_MIN;
    if (raw > ACCEL_RAW_MAX) raw = ACCEL_RAW_MAX;
    return sendParam(Tasks::MotorCmdType::SetAccel, raw);
}

Result setDecelPhysical(uint32_t stepsPerSecSq) {
    if (stepsPerSecSq == 0 || stepsPerSecSq > ACCEL_PHYS_MAX) {
        return Result::INVALID_PARAM;
    }
    uint16_t raw = accelPhysicalToRaw(stepsPerSecSq);
    if (raw < ACCEL_RAW_MIN) raw = ACCEL_RAW_MIN;
    if (raw > ACCEL_RAW_MAX) raw = ACCEL_RAW_MAX;
    return sendParam(Tasks::MotorCmdType::SetDecel, raw);
}

Result setMaxSpeedPhysical(uint32_t stepsPerSec) {
    if (stepsPerSec == 0 || stepsPerSec > MAXSPD_PHYS_MAX) {
        return Result::INVALID_PARAM;
    }
    uint16_t raw = maxSpeedPhysicalToRaw(stepsPerSec);
    if (raw < MAXSPD_RAW_MIN) raw = MAXSPD_RAW_MIN;
    if (raw > MAXSPD_RAW_MAX) raw = MAXSPD_RAW_MAX;
    return sendParam(Tasks::MotorCmdType::SetMaxSpeed, raw);
}

// --- Raw register setters (legacy commands) ---

Result setAccelRaw(uint16_t raw) {
    if (raw < ACCEL_RAW_MIN || raw > ACCEL_RAW_MAX) {
        return Result::INVALID_PARAM;
    }
    return sendParam(Tasks::MotorCmdType::SetAccel, raw);
}

Result setDecelRaw(uint16_t raw) {
    if (raw < ACCEL_RAW_MIN || raw > ACCEL_RAW_MAX) {
        return Result::INVALID_PARAM;
    }
    return sendParam(Tasks::MotorCmdType::SetDecel, raw);
}

Result setMaxSpeedRaw(uint16_t raw) {
    if (raw < MAXSPD_RAW_MIN || raw > MAXSPD_RAW_MAX) {
        return Result::INVALID_PARAM;
    }
    return sendParam(Tasks::MotorCmdType::SetMaxSpeed, raw);
}

} // namespace Services::Config
