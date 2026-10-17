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

#include "L3_services/boundary/imotor_command_sink.hpp"
#include "L3_services/boundary/iemergency_stop.hpp"
#include "L3_services/boundary/iimmediate_stop.hpp"
#include "L3_services/boundary/ischeduled_start.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/iclock.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Services {

// Consumer port types used in this module
using Services::IMotorCommandSink;
using Services::MotorCmdType;
using Services::MotorCommand;

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
  TIMEOUT_EXPIRED, // START_AT tick already passed
  MOTOR_QUEUE_FULL // motor task queue full: remaining commands stay pending
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
class CommandQueue : public Services::IEmergencyStop, public Services::IScheduledStart {
public:
  static constexpr uint32_t FLUSH_SEND_TIMEOUT_MS = 50;   ///< bounded per-command send on START
  /**
   * @brief Initialize the command queue service
   * @return true on success
   */
  bool init(Platform::ILock& lock, Platform::IClock& clock, IMotorCommandSink& sink,
            IImmediateStop* immediateStop = nullptr);

  /**
   * @brief Get current controller state
   */
  /// ESTOP is derived from the MotorService latch (the single holder, Phase 4 step 4): a stop
  /// from any path (SYST:ESTOP, the watchdog, the out-of-band byte) shows here as ESTOP.
  ControllerState getState() const {
    return (m_immediate != nullptr && m_immediate->isStopLatched()) ? ControllerState::ESTOP : m_state;
  }

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
   * @brief Schedule execution at a TickTimer tick (SYNC:START:AT)
   *
   * Only allowed in ARMED state. Returns TIMEOUT_EXPIRED when the tick has passed and
   * INVALID_PARAM when it is more than MAX_START_AT_US ahead. Otherwise hands a StartAt
   * command to the Motor task and returns at once (OK = scheduled); the Motor task waits
   * for the tick and calls executeScheduledStart(). The Comms task never blocks, so an
   * ESTOP or the watchdog is handled during the wait.
   *
   * @param targetTick Tick timestamp to start execution
   * @return QueueResult::OK or error code
   */
  QueueResult startAt(uint32_t targetTick);

  /// Longest wait a SYNC:START:AT may ask for (the Motor task stops polling meanwhile).
  static constexpr uint32_t MAX_START_AT_US = 2000000;

  // IScheduledStart (Motor task context)
  bool scheduledStartPending() const override { return m_scheduled; }
  bool executeScheduledStart() override;

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
  void emergencyStop() override;

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
  volatile bool m_scheduled = false;   // SYNC:START:AT armed, Motor task will flush
  uint32_t m_scheduledTick = 0;

  // Platform abstractions
  Platform::ILock* m_lock;
  Platform::IClock* m_clock;
  IMotorCommandSink* m_sink;
  IImmediateStop* m_immediate = nullptr;

  // Helper to flush pending commands to motor task
  bool flushToMotorTask();
};

// Global instance
extern CommandQueue g_commandQueue;

} // namespace Services
