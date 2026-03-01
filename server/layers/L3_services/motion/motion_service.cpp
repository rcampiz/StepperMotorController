/**
 * @file motion_service.cpp
 * @brief Motion service implementation — wraps MotorTask_SendCommand
 */

#include "L3_services/motion/motion_service.hpp"
#include "L3_services/config/config_service.hpp"
#include "F_platform/interfaces/imotor_command_sink.hpp"
#include "L3_services/infra/trace.hpp"

namespace Services::Motion {

const char *resultToString(Result r) {
    switch (r) {
    case Result::OK:            return "OK";
    case Result::QUEUE_FULL:    return "Queue full";
    case Result::INVALID_PARAM: return "Invalid parameter";
    case Result::FAULT:         return "Fault";
    default:                    return "Unknown";
    }
}

static Result sendCmd(MotorCmdType type, int32_t p1 = 0, int32_t p2 = 0) {
    MotorCommand cmd = {};
    cmd.type = type;
    cmd.param1 = p1;
    cmd.param2 = p2;
    return g_motorCommandSink->sendCommand(cmd) ? Result::OK : Result::QUEUE_FULL;
}

// Convert steps/s to raw MAX_SPEED register value (10-bit, 0 = no override)
static int32_t speedOverrideRaw(uint32_t stepsPerSec) {
    if (stepsPerSec == 0) return 0;
    uint16_t raw = Config::maxSpeedPhysicalToRaw(stepsPerSec);
    if (raw < 1) raw = 1;
    if (raw > 1023) raw = 1023;
    return static_cast<int32_t>(raw);
}

Result run(uint32_t stepsPerSec, bool forward) {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:RUN", stepsPerSec);
    uint32_t speedRaw = static_cast<uint32_t>(
        (static_cast<uint64_t>(stepsPerSec) * 1048576ULL) / 15625ULL);
    Result r = sendCmd(MotorCmdType::Run,
                       static_cast<int32_t>(speedRaw),
                       forward ? 1 : 0);
    TRACE_EXIT("MOT:RUN", static_cast<uint32_t>(r));
    return r;
}

Result move(int32_t steps, uint32_t maxSpeedStepsPerSec) {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:MOVE", static_cast<uint32_t>(steps));
    Result r = sendCmd(MotorCmdType::Move, steps,
                       speedOverrideRaw(maxSpeedStepsPerSec));
    TRACE_EXIT("MOT:MOVE", static_cast<uint32_t>(r));
    return r;
}

Result goTo(int32_t position, uint32_t maxSpeedStepsPerSec) {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:GOTO", static_cast<uint32_t>(position));
    Result r = sendCmd(MotorCmdType::GoTo, position,
                       speedOverrideRaw(maxSpeedStepsPerSec));
    TRACE_EXIT("MOT:GOTO", static_cast<uint32_t>(r));
    return r;
}

Result stop(bool hard) {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:STOP", hard ? 1U : 0U);
    Result r = sendCmd(hard ? MotorCmdType::HardStop
                            : MotorCmdType::SoftStop);
    TRACE_EXIT("MOT:STOP", static_cast<uint32_t>(r));
    return r;
}

Result enable() {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:EN");
    Result r = sendCmd(MotorCmdType::HardStop);
    if (r == Result::OK) {
        sendCmd(MotorCmdType::GetStatus);
    }
    TRACE_EXIT("MOT:EN", static_cast<uint32_t>(r));
    return r;
}

Result disable() {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:DIS");
    Result r = sendCmd(MotorCmdType::SoftHiZ);
    TRACE_EXIT("MOT:DIS", static_cast<uint32_t>(r));
    return r;
}

Result home(uint32_t maxSpeedStepsPerSec) {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:HOME");
    Result r = sendCmd(MotorCmdType::GoHome, 0,
                       speedOverrideRaw(maxSpeedStepsPerSec));
    TRACE_EXIT("MOT:HOME", static_cast<uint32_t>(r));
    return r;
}

Result zero() {
    Trace::ServiceScope svc(Trace::SVC_MOTION);
    TRACE_ENTRY("MOT:ZERO");
    Result r = sendCmd(MotorCmdType::ResetPos);
    TRACE_EXIT("MOT:ZERO", static_cast<uint32_t>(r));
    return r;
}

} // namespace Services::Motion
