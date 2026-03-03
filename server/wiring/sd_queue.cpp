/**
 * @file sd_queue.cpp
 * @brief ServiceDispatcher — IQueueDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/dispatch/command_queue.hpp"

static Comms::ServiceStatus mapQueueResult(Services::QueueResult r) {
    switch (r) {
        case Services::QueueResult::OK:              return Comms::ServiceStatus::Ok;
        case Services::QueueResult::QUEUE_FULL:      return Comms::ServiceStatus::QueueFull;
        case Services::QueueResult::QUEUE_EMPTY:     return Comms::ServiceStatus::QueueEmpty;
        case Services::QueueResult::INVALID_STATE:   return Comms::ServiceStatus::InvalidState;
        case Services::QueueResult::INVALID_COMMAND: return Comms::ServiceStatus::InvalidCommand;
        case Services::QueueResult::INVALID_PARAM:   return Comms::ServiceStatus::InvalidParam;
        case Services::QueueResult::TIMEOUT_EXPIRED: return Comms::ServiceStatus::TimeoutExpired;
        default:                                     return Comms::ServiceStatus::InvalidParam;
    }
}

Comms::ServiceStatus ServiceDispatcher::queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) {
    Services::MotorCommand cmd = {};
    cmd.type = static_cast<Services::MotorCmdType>(cmdType);
    cmd.param1 = p1;
    cmd.param2 = p2;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueMove(int32_t signedSteps) {
    Services::MotorCommand cmd = {};
    cmd.type = Services::MotorCmdType::Move;
    cmd.param1 = signedSteps;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueGoTo(int32_t position) {
    Services::MotorCommand cmd = {};
    cmd.type = Services::MotorCmdType::GoTo;
    cmd.param1 = position;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueRun(uint32_t speedRaw, int32_t direction) {
    Services::MotorCommand cmd = {};
    cmd.type = Services::MotorCmdType::Run;
    cmd.param1 = static_cast<int32_t>(speedRaw);
    cmd.param2 = direction;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueStop(bool hard) {
    Services::MotorCommand cmd = {};
    cmd.type = hard ? Services::MotorCmdType::HardStop : Services::MotorCmdType::SoftStop;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueHome() {
    Services::MotorCommand cmd = {};
    cmd.type = Services::MotorCmdType::GoHome;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueZero() {
    Services::MotorCommand cmd = {};
    cmd.type = Services::MotorCmdType::ResetPos;
    return mapQueueResult(Services::g_commandQueue.queueCommand(cmd));
}

Comms::ServiceStatus ServiceDispatcher::queueArm() {
    return mapQueueResult(Services::g_commandQueue.arm());
}

Comms::ServiceStatus ServiceDispatcher::queueStart() {
    return mapQueueResult(Services::g_commandQueue.start());
}

Comms::ServiceStatus ServiceDispatcher::queueStartAt(uint32_t targetTick) {
    return mapQueueResult(Services::g_commandQueue.startAt(targetTick));
}

Comms::ServiceStatus ServiceDispatcher::queueClear() {
    return mapQueueResult(Services::g_commandQueue.clearQueue());
}

uint8_t ServiceDispatcher::getControllerState() {
    return static_cast<uint8_t>(Services::g_commandQueue.getState());
}

uint32_t ServiceDispatcher::getQueueDepth() {
    return static_cast<uint32_t>(Services::g_commandQueue.getQueueDepth());
}

