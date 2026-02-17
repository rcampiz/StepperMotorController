/**
 * @file motion_service.cpp
 * @brief Motion service implementation — wraps MotorTask_SendCommand
 */

#include "services/motion_service.hpp"
#include "services/trace.hpp"
#include "tasks/motor_task.hpp"

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

static Result sendCmd(Tasks::MotorCmdType type, int32_t p1 = 0, int32_t p2 = 0) {
    Tasks::MotorCommand cmd = {};
    cmd.type = type;
    cmd.param1 = p1;
    cmd.param2 = p2;
    return Tasks::MotorTask_SendCommand(cmd) ? Result::OK : Result::QUEUE_FULL;
}

Result run(uint32_t stepsPerSec, bool forward) {
    TRACE_ENTRY("MOT:RUN", stepsPerSec);
    // Convert steps/s to raw register value for powerSTEP01
    // Formula: raw = steps_s * 1048576 / 15625
    uint32_t speedRaw = static_cast<uint32_t>(
        (static_cast<uint64_t>(stepsPerSec) * 1048576ULL) / 15625ULL);
    Result r = sendCmd(Tasks::MotorCmdType::Run,
                       static_cast<int32_t>(speedRaw),
                       forward ? 1 : 0);
    TRACE_EXIT("MOT:RUN", static_cast<uint32_t>(r));
    return r;
}

Result move(int32_t steps) {
    TRACE_ENTRY("MOT:MOVE", static_cast<uint32_t>(steps));
    Result r = sendCmd(Tasks::MotorCmdType::Move, steps);
    TRACE_EXIT("MOT:MOVE", static_cast<uint32_t>(r));
    return r;
}

Result goTo(int32_t position) {
    TRACE_ENTRY("MOT:GOTO", static_cast<uint32_t>(position));
    Result r = sendCmd(Tasks::MotorCmdType::GoTo, position);
    TRACE_EXIT("MOT:GOTO", static_cast<uint32_t>(r));
    return r;
}

Result stop(bool hard) {
    TRACE_ENTRY("MOT:STOP", hard ? 1U : 0U);
    Result r = sendCmd(hard ? Tasks::MotorCmdType::HardStop
                            : Tasks::MotorCmdType::SoftStop);
    TRACE_EXIT("MOT:STOP", static_cast<uint32_t>(r));
    return r;
}

Result enable() {
    TRACE_ENTRY("MOT:EN");
    // HardStop exits Hi-Z and enables the gate driver (holds position).
    // Then read STATUS to clear any latched fault flags.
    Result r = sendCmd(Tasks::MotorCmdType::HardStop);
    if (r == Result::OK) {
        sendCmd(Tasks::MotorCmdType::GetStatus);
    }
    TRACE_EXIT("MOT:EN", static_cast<uint32_t>(r));
    return r;
}

Result disable() {
    TRACE_ENTRY("MOT:DIS");
    Result r = sendCmd(Tasks::MotorCmdType::SoftHiZ);
    TRACE_EXIT("MOT:DIS", static_cast<uint32_t>(r));
    return r;
}

Result home() {
    TRACE_ENTRY("MOT:HOME");
    Result r = sendCmd(Tasks::MotorCmdType::GoHome);
    TRACE_EXIT("MOT:HOME", static_cast<uint32_t>(r));
    return r;
}

Result zero() {
    TRACE_ENTRY("MOT:ZERO");
    Result r = sendCmd(Tasks::MotorCmdType::ResetPos);
    TRACE_EXIT("MOT:ZERO", static_cast<uint32_t>(r));
    return r;
}

} // namespace Services::Motion
