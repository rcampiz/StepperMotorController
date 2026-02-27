/**
 * @file command_queue.cpp
 * @brief Command queue service implementation
 */

#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "FreeRTOS.h"
#include "semphr.h"

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
        default:                           return "Unknown error";
    }
}

bool CommandQueue::init()
{
    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        return false;
    }

    m_queueHead = 0;
    m_queueTail = 0;
    m_queueDepth = 0;
    m_state = ControllerState::IDLE;

    return true;
}

bool CommandQueue::lock(TickType_t timeout)
{
    return xSemaphoreTake(m_mutex, timeout) == pdTRUE;
}

void CommandQueue::unlock()
{
    xSemaphoreGive(m_mutex);
}

QueueResult CommandQueue::queueCommand(const Tasks::MotorCommand& cmd)
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

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

    unlock();
    return result;
}

QueueResult CommandQueue::arm()
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

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

    unlock();
    return result;
}

QueueResult CommandQueue::start()
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::ARMED) {
        result = QueueResult::INVALID_STATE;
    }
    else {
        // Flush commands to motor task
        flushToMotorTask();
        m_state = ControllerState::RUNNING;
    }

    unlock();
    return result;
}

QueueResult CommandQueue::startAt(uint32_t targetTick)
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

    QueueResult result = QueueResult::OK;

    if (m_state != ControllerState::ARMED) {
        result = QueueResult::INVALID_STATE;
        unlock();
        return result;
    }

    // Check if target tick has already passed
    uint32_t now = TickTimer_GetTick();
    int32_t delta = static_cast<int32_t>(targetTick - now);

    if (delta < 0) {
        // Target tick already passed
        result = QueueResult::TIMEOUT_EXPIRED;
        unlock();
        return result;
    }

    // Release mutex during wait (allows other operations)
    unlock();

    // Busy-wait until target tick (for precise timing)
    // For longer waits, could use vTaskDelayUntil for most of the time
    if (delta > 1000) {
        // For waits > 1ms, sleep for most of it
        vTaskDelay(pdMS_TO_TICKS(delta / 1000));
    }

    // Spin-wait for final precision
    while (static_cast<int32_t>(targetTick - TickTimer_GetTick()) > 0) {
        // Tight loop for microsecond precision
    }

    // Re-acquire lock and execute
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

    // Verify still in ARMED state (could have been cancelled)
    if (m_state != ControllerState::ARMED) {
        result = QueueResult::INVALID_STATE;
    }
    else {
        flushToMotorTask();
        m_state = ControllerState::RUNNING;
    }

    unlock();
    return result;
}

QueueResult CommandQueue::clearQueue()
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

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

    unlock();
    return result;
}

void CommandQueue::emergencyStop()
{
    // Don't wait for lock - emergency action
    if (lock(pdMS_TO_TICKS(10))) {
        // Clear pending queue
        m_queueHead = 0;
        m_queueTail = 0;
        m_queueDepth = 0;
        m_state = ControllerState::ESTOP;
        unlock();
    }
    else {
        // Couldn't get lock, force state anyway
        m_state = ControllerState::ESTOP;
    }

    // Send hard stop to motor (outside lock to avoid deadlock)
    Tasks::MotorCommand stopCmd = {};
    stopCmd.type = Tasks::MotorCmdType::HardHiZ;
    Tasks::MotorTask_SendCommand(stopCmd, 0);
}

QueueResult CommandQueue::clearFault()
{
    if (!lock()) {
        return QueueResult::INVALID_STATE;
    }

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

    unlock();
    return result;
}

void CommandQueue::notifyMotionComplete()
{
    if (lock(pdMS_TO_TICKS(100))) {
        if (m_state == ControllerState::RUNNING) {
            m_state = ControllerState::IDLE;
        }
        unlock();
    }
}

void CommandQueue::notifyFault(const char* reason)
{
    (void)reason; // Could log this

    if (lock(pdMS_TO_TICKS(100))) {
        m_state = ControllerState::FAULT;
        // Don't clear queue - allow inspection
        unlock();
    }
}

void CommandQueue::flushToMotorTask()
{
    // Called with lock held
    while (m_queueDepth > 0) {
        Tasks::MotorCommand& cmd = m_pendingCmds[m_queueHead];
        Tasks::MotorTask_SendCommand(cmd, portMAX_DELAY);
        m_queueHead = (m_queueHead + 1) % CMD_QUEUE_MAX_DEPTH;
        m_queueDepth--;
    }
}

} // namespace Services
