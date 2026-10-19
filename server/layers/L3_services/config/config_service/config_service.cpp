/**
 * @file config_service.cpp
 * @brief Configuration service — live ramps and speed ceiling in physical units
 */

#include "L3_services/config/config_service/config_service.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/trace/trace.hpp"

namespace Services::Config {

// Harness pin types used in this module
using Services::MotorCmdType;
using Services::MotorCommand;
static Services::IMotorCommandSink* const g_motorCommandSink = &Services::g_motorService;

const char *resultToString(Result r) {
    switch (r) {
    case Result::OK:            return "OK";
    case Result::QUEUE_FULL:    return "Queue full";
    case Result::INVALID_PARAM: return "Invalid parameter";
    default:                    return "Unknown";
    }
}

// --- Internal send helper ---

static Result sendParam(MotorCmdType type, int32_t value) {
    MotorCommand cmd = {};
    cmd.type = type;
    cmd.param1 = value;
    return g_motorCommandSink->sendCommand(cmd) ? Result::OK : Result::QUEUE_FULL;
}

/// 1..the driver's maximum (a request below the driver's resolution gets its smallest step)
static bool inRange(uint32_t value, float max) {
    return value != 0 && static_cast<float>(value) <= max;
}

// --- Physical-unit setters (SCPI commands) ---

Result setAccelPhysical(uint32_t stepsPerSecSq) {
    Trace::ServiceScope svc(Trace::SVC_CONFIG);
    if (!inRange(stepsPerSecSq, motion.stepper.config.caps().accelMaxFsps2)) {
        return Result::INVALID_PARAM;
    }
    return sendParam(MotorCmdType::SetAccel, static_cast<int32_t>(stepsPerSecSq));
}

Result setDecelPhysical(uint32_t stepsPerSecSq) {
    Trace::ServiceScope svc(Trace::SVC_CONFIG);
    if (!inRange(stepsPerSecSq, motion.stepper.config.caps().accelMaxFsps2)) {
        return Result::INVALID_PARAM;
    }
    return sendParam(MotorCmdType::SetDecel, static_cast<int32_t>(stepsPerSecSq));
}

Result setMaxSpeedPhysical(uint32_t stepsPerSec) {
    Trace::ServiceScope svc(Trace::SVC_CONFIG);
    if (!inRange(stepsPerSec, motion.stepper.config.caps().maxSpeedLimitFsps)) {
        return Result::INVALID_PARAM;
    }
    return sendParam(MotorCmdType::SetMaxSpeed, static_cast<int32_t>(stepsPerSec));
}

} // namespace Services::Config
