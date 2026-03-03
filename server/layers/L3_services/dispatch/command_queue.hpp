/**
 * @file command_queue.hpp
 * @brief Command queue service for ARM/START synchronization
 *
 * Implements the synchronized multi-controller command protocol:
 *   1. QUEUE <cmd> - Add command to pending queue
 *   2. ARM - Validate queue and prepare for execution
 *   3. START - Execute queued commands immediately
 *   4. START_AT <tick> - Execute at specified tick timestamp
 *
 * State machine:
 *   IDLE -> (QUEUE commands) -> ARM -> ARMED -> START -> RUNNING -> IDLE
 *                                  |                          |
 *                                  +<-- CLEAR_QUEUE <---------+
 *
 * Thread-safe: Uses ILock for state protection.
 */

#pragma once

#include "F_platform/hal/ilock.hpp"
#include "F_platform/hal/iclock.hpp"
#include "F_platform/hal/imotor_command_sink.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Services {

// Maximum commands in the pending queue
constexpr size_t CMD_QUEUE_MAX_DEPTH = 8;

/**
 * @brief Controller state machine states
 */
enum class ControllerState : uint8_t {
  IDLE,    // Ready for commands, queue can be modified
  ARMED,   // Commands queued, awaiting START/START_AT
  RUNNING, // Motion in progress
  FAULT,   // Error condition, requires CLEAR_FAULT
  ESTOP    // Emergency stop, outputs disabled
};

/**
 * @brief Result codes for queue operations
 */
enum class QueueResult : uint8_t {
  OK,
  QUEUE_FULL,
  QUEUE_EMPTY,
  INVALID_STATE,   // Operation not allowed in current state
  INVALID_COMMAND, // Unrecognized command
  INVALID_PARAM,   // Bad parameter value
  TIMEOUT_EXPIRED  // START_AT tick already passed
};

/**
 * @brief Convert state to string for responses
 */
const char *stateToString(ControllerState state);

/**
 * @brief Convert result to string for responses
 */
const char *resultToString(QueueResult result);

/**
 * @brief Command queue manager
 *
 * Singleton that manages the ARM/START synchronization protocol.
 */
class CommandQueue {
public:
  /**
   * @brief Initialize the command queue service
   * @return true on success
   */
  bool init(ILock& lock, IClock& clock, IMotorCommandSink& sink);

  /**
   * @brief Get current controller state
   */
  ControllerState getState() const { return m_state; }

  /**
   * @brief Get number of commands in pending queue
   */
  size_t getQueueDepth() const { return m_queueDepth; }

  /**
   * @brief Add command to pending queue
   *
   * Only allowed in IDLE state.
   *
   * @param cmd Motor command to queue
   * @return QueueResult::OK or error code
   */
  QueueResult queueCommand(const MotorCommand &cmd);

  /**
   * @brief Transition from IDLE to ARMED state
   *
   * Validates that queue is not empty and system is ready.
   *
   * @return QueueResult::OK or error code
   */
  QueueResult arm();

  /**
   * @brief Execute queued commands immediately
   *
   * Only allowed in ARMED state. Flushes pending queue
   * to motor task and transitions to RUNNING.
   *
   * @return QueueResult::OK or error code
   */
  QueueResult start();

  /**
   * @brief Schedule execution at specific tick timestamp
   *
   * Only allowed in ARMED state. If tick has already passed,
   * returns TIMEOUT_EXPIRED. Otherwise waits until tick
   * then executes.
   *
   * @param targetTick Tick timestamp to start execution
   * @return QueueResult::OK or error code
   */
  QueueResult startAt(uint32_t targetTick);

  /**
   * @brief Clear pending queue and return to IDLE
   *
   * Allowed in IDLE or ARMED state. In RUNNING state,
   * use stop() first.
   *
   * @return QueueResult::OK or error code
   */
  QueueResult clearQueue();

  /**
   * @brief Emergency stop - clear queue, stop motion, go to ESTOP
   *
   * Allowed in any state. Sends HardStop to motor task.
   */
  void emergencyStop();

  /**
   * @brief Clear fault/estop and return to IDLE
   *
   * Only allowed in FAULT or ESTOP state.
   *
   * @return QueueResult::OK or error code
   */
  QueueResult clearFault();

  /**
   * @brief Notify that motion is complete
   *
   * Called by motor task when all queued commands finish.
   * Transitions from RUNNING to IDLE.
   */
  void notifyMotionComplete();

  /**
   * @brief Notify of a fault condition
   *
   * Called by motor task on error. Transitions to FAULT.
   *
   * @param reason Fault description for logging
   */
  void notifyFault(const char *reason);

private:
  // Pending command buffer (simple ring buffer)
  MotorCommand m_pendingCmds[CMD_QUEUE_MAX_DEPTH];
  size_t m_queueHead;
  size_t m_queueTail;
  size_t m_queueDepth;

  // State
  ControllerState m_state;

  // Platform abstractions
  ILock* m_lock;
  IClock* m_clock;
  IMotorCommandSink* m_sink;

  // Helper to flush pending commands to motor task
  void flushToMotorTask();
};

// Global instance
extern CommandQueue g_commandQueue;

} // namespace Services
