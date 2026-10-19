/**
 * @file queue_dispatch.cpp
 * @brief L3 IQueueDispatcher implementation
 */

#include "queue_dispatch.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"

namespace Services {

static Harness::ServiceStatus mapQueueResult(QueueResult r) {
    switch (r) {
        case QueueResult::OK:              return Harness::ServiceStatus::Ok;
        case QueueResult::QUEUE_FULL:      return Harness::ServiceStatus::QueueFull;
        case QueueResult::QUEUE_EMPTY:     return Harness::ServiceStatus::QueueEmpty;
        case QueueResult::INVALID_STATE:   return Harness::ServiceStatus::InvalidState;
        case QueueResult::INVALID_COMMAND: return Harness::ServiceStatus::InvalidCommand;
        case QueueResult::INVALID_PARAM:   return Harness::ServiceStatus::InvalidParam;
        case QueueResult::TIMEOUT_EXPIRED: return Harness::ServiceStatus::TimeoutExpired;
        default:                           return Harness::ServiceStatus::InvalidParam;
    }
}

Harness::ServiceStatus QueueDispatch::queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) {
    MotorCommand cmd = {};
    cmd.type = static_cast<MotorCmdType>(cmdType);
    cmd.param1 = p1;
    cmd.param2 = p2;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueMove(int32_t signedSteps) {
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::Move;
    cmd.param1 = signedSteps;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueGoTo(int32_t position) {
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::GoTo;
    cmd.param1 = position;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueRun(uint32_t stepsPerSec, int32_t direction) {
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::Run;
    cmd.param1 = static_cast<int32_t>(stepsPerSec);     // full steps/s
    cmd.param2 = direction;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueStop(bool hard) {
    MotorCommand cmd = {};
    cmd.type = hard ? MotorCmdType::HardStop : MotorCmdType::SoftStop;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueHome() {
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::GoHome;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueZero() {
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::ResetPos;
    return mapQueueResult(g_commandQueue.queueCommand(cmd));
}

Harness::ServiceStatus QueueDispatch::queueArm() {
    return mapQueueResult(g_commandQueue.arm());
}

Harness::ServiceStatus QueueDispatch::queueStart() {
    return mapQueueResult(g_commandQueue.start());
}

Harness::ServiceStatus QueueDispatch::queueStartAt(uint32_t targetTick) {
    return mapQueueResult(g_commandQueue.startAt(targetTick));
}

Harness::ServiceStatus QueueDispatch::queueClear() {
    return mapQueueResult(g_commandQueue.clearQueue());
}

uint8_t QueueDispatch::getControllerState() {
    return static_cast<uint8_t>(g_commandQueue.getState());
}

uint32_t QueueDispatch::getQueueDepth() {
    return static_cast<uint32_t>(g_commandQueue.getQueueDepth());
}

} // namespace Services
