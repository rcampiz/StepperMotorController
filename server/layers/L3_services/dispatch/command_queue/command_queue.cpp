/**
 * @file command_queue.cpp
 * @brief Command queue service implementation
 */

#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "harness/trace/interface_trace.hpp"

namespace Services {

// Global instance
CommandQueue g_commandQueue;

const char* stateToString(ControllerState state)
{
    switch (state) {
        case ControllerState::IDLE:    return "IDLE";
        case ControllerState::ARMED:   return "ARMED";
        case ControllerState::RUNNING: return "RUNNING";
        case ControllerState::FAULT:   return "FAULT";
        case ControllerState::ESTOP:   return "ESTOP";
        default:                       return "UNKNOWN";
    }
}

const char* resultToString(QueueResult result)
{
    switch (result) {
        case QueueResult::OK:              return "OK";
        case QueueResult::QUEUE_FULL:      return "Queue full";
        case QueueResult::QUEUE_EMPTY:     return "Queue empty";
        case QueueResult::INVALID_STATE:   return "Invalid state";
        case QueueResult::INVALID_COMMAND: return "Invalid command";
        case QueueResult::INVALID_PARAM:   return "Invalid parameter";
        case QueueResult::TIMEOUT_EXPIRED: return "Timeout expired";
        case QueueResult::MOTOR_QUEUE_FULL: return "Motor queue full";
        default:                           return "Unknown error";
    }
}

bool CommandQueue::init(Platform::ILock& lock, Platform::IClock& clock, IMotorCommandSink& sink,
                        IImmediateStop* immediateStop)
{
    m_lock = &lock;
    m_clock = &clock;
    m_sink = &sink;
    m_immediate = immediateStop;

    m_queueHead = 0;
    m_queueTail = 0;
    m_queueDepth = 0;
    m_state = ControllerState::IDLE;

    return true;
}

QueueResult CommandQueue::queueCommand(const MotorCommand& cmd)
{
    m_lock->acquire();

    QueueResult result = QueueResult::OK;

    // Only allow queuing in IDLE state
    if (m_state != ControllerState::IDLE) {
        result = QueueResult::INVALID_STATE;
    }
    // Check for space
    else if (m_queueDepth >= CMD_QUEUE_MAX_DEPTH) {
        result = QueueResult::QUEUE_FULL;
    }
    else {
        // Add to queue
        m_pendingCmds[m_queueTail] = cmd;
        m_queueTail = (m_queueTail + 1) % CMD_QUEUE_MAX_DEPTH;
        m_queueDepth++;
    }

    m_lock->release();
    return result;
}

QueueResult CommandQueue::arm()
{
    m_lock->acquire();

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::IDLE) {
        result = QueueResult::INVALID_STATE;
    }
    else if (m_queueDepth == 0) {
        result = QueueResult::QUEUE_EMPTY;
    }
    else {
        m_state = ControllerState::ARMED;
    }

    m_lock->release();
    return result;
}

QueueResult CommandQueue::start()
{
    m_lock->acquire();

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::ARMED) {
        result = QueueResult::INVALID_STATE;
    }
    else {
        // Flush commands to motor task (bounded: a full motor queue reports MOTOR_QUEUE_FULL)
        if (!flushToMotorTask()) {
            result = QueueResult::MOTOR_QUEUE_FULL;
        }
        m_state = ControllerState::RUNNING;
    }

    m_lock->release();
    return result;
}

QueueResult CommandQueue::startAt(uint32_t targetTick)
{
    m_lock->acquire();

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::ARMED) {
        result = QueueResult::INVALID_STATE;
    }
    else {
        // Validate the instant here; the waiting is the Motor task's job (it owns the
        // driver and can spin to the microsecond without holding the Comms task).
        uint32_t now = TickTimer_GetTick();
        auto delta = static_cast<int32_t>(targetTick - now);
        if (delta < 0) {
            result = QueueResult::TIMEOUT_EXPIRED;
        }
        else if (static_cast<uint32_t>(delta) > MAX_START_AT_US) {
            result = QueueResult::INVALID_PARAM;
        }
        else {
            MotorCommand cmd;
            cmd.type = MotorCmdType::StartAt;
            cmd.param1 = static_cast<int32_t>(targetTick);
            cmd.param2 = 0;
            m_scheduledTick = targetTick;
            m_scheduled = true;
            if (!m_sink->sendCommand(cmd, FLUSH_SEND_TIMEOUT_MS)) {
                m_scheduled = false;
                result = QueueResult::MOTOR_QUEUE_FULL;
            }
        }
    }

    m_lock->release();
    return result;
}

bool CommandQueue::executeScheduledStart()
{
    m_lock->acquire();
    bool ok = false;
    if (m_scheduled && m_state == ControllerState::ARMED) {
        ok = flushToMotorTask();
        m_state = ControllerState::RUNNING;
    }
    m_scheduled = false;
    m_lock->release();
    return ok;
}

QueueResult CommandQueue::clearQueue()
{
    m_lock->acquire();
    m_scheduled = false;

    QueueResult result = QueueResult::OK;

    if (m_state == ControllerState::RUNNING) {
        // Can't clear while running - need to stop first
        result = QueueResult::INVALID_STATE;
    }
    else if (m_state == ControllerState::FAULT || m_state == ControllerState::ESTOP) {
        // Can clear in fault/estop states
        m_queueHead = 0;
        m_queueTail = 0;
        m_queueDepth = 0;
    }
    else {
        // IDLE or ARMED - clear and go to IDLE
        m_queueHead = 0;
        m_queueTail = 0;
        m_queueDepth = 0;
        m_state = ControllerState::IDLE;
    }

    m_lock->release();
    return result;
}

void CommandQueue::emergencyStop()
{
    m_scheduled = false;
    ITRACE(Harness::ITrace::L3_CMD_SINK, "[L3~L3]", "emergencyStop");

    // Acquire lock — held briefly, safe for emergency path
    m_lock->acquire();

    // Clear pending queue
    m_queueHead = 0;
    m_queueTail = 0;
    m_queueDepth = 0;
    m_state = ControllerState::ESTOP;

    m_lock->release();

    // Stop the motor NOW, bypassing the command queue (a full queue must never delay ESTOP)
    if (m_immediate != nullptr) {
        m_immediate->immediateHardHiZ();
    }
    // Also queue HardHiZ so the Motor task sees the stop in order (best effort, no wait)
    MotorCommand stopCmd = {};
    stopCmd.type = MotorCmdType::HardHiZ;
    m_sink->sendCommand(stopCmd, 0);
}

QueueResult CommandQueue::clearFault()
{
    m_lock->acquire();

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::FAULT && m_state != ControllerState::ESTOP) {
        result = QueueResult::INVALID_STATE;
    }
    else {
        // Clear queue and return to IDLE
        m_queueHead = 0;
        m_queueTail = 0;
        m_queueDepth = 0;
        m_state = ControllerState::IDLE;
    }

    m_lock->release();
    if (result == QueueResult::OK && m_immediate != nullptr) {
        m_immediate->clearImmediateStop();   // Motor task accepts motion again from its next pass
    }
    return result;
}

void CommandQueue::notifyMotionComplete()
{
    m_lock->acquire();
    if (m_state == ControllerState::RUNNING) {
        m_state = ControllerState::IDLE;
    }
    m_lock->release();
}

void CommandQueue::notifyFault(const char* reason)
{
    (void)reason; // Could log this

    m_lock->acquire();
    m_state = ControllerState::FAULT;
    // Don't clear queue - allow inspection
    m_lock->release();
}

bool CommandQueue::flushToMotorTask()
{
    // Called with lock held. Bounded send: never block the Comms task (and its heartbeat
    // watchdog) forever. Unsent commands stay pending and the caller reports MOTOR_QUEUE_FULL.
    while (m_queueDepth > 0) {
        MotorCommand& cmd = m_pendingCmds[m_queueHead];
        if (!m_sink->sendCommand(cmd, FLUSH_SEND_TIMEOUT_MS)) {
            return false;
        }
        m_queueHead = (m_queueHead + 1) % CMD_QUEUE_MAX_DEPTH;
        m_queueDepth--;
    }
    return true;
}

} // namespace Services
