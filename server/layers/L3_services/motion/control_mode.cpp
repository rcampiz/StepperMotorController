/**
 * @file control_mode.cpp
 * @brief Control mode manager implementation
 */

#include "L3_services/motion/control_mode.hpp"
#include <string.h>

namespace Services {

// Global instance
ControlModeManager g_controlMode;

const char *modeToString(ControlMode mode) {
  switch (mode) {
  case ControlMode::OPEN_LOOP:
    return "OPEN_LOOP";
  case ControlMode::CLOSED_LOOP_MONITOR:
    return "CLOSED_LOOP_MONITOR";
  case ControlMode::SPEED_TRIM:
    return "SPEED_TRIM";
  default:
    return "UNKNOWN";
  }
}

ControlMode parseMode(const char *str) {
  // OPEN_LOOP
  if (strcmp(str, "OPEN_LOOP") == 0 || strcmp(str, "open_loop") == 0 ||
      strcmp(str, "OPEN") == 0 || strcmp(str, "open") == 0 ||
      strcmp(str, "0") == 0) {
    return ControlMode::OPEN_LOOP;
  }
  // CLOSED_LOOP_MONITOR (also accepts legacy "CLOSED_LOOP" / "1")
  if (strcmp(str, "CLOSED_LOOP_MONITOR") == 0 ||
      strcmp(str, "closed_loop_monitor") == 0 ||
      strcmp(str, "MONITOR") == 0 || strcmp(str, "monitor") == 0 ||
      strcmp(str, "CLOSED_LOOP") == 0 || strcmp(str, "closed_loop") == 0 ||
      strcmp(str, "CLOSED") == 0 || strcmp(str, "closed") == 0 ||
      strcmp(str, "1") == 0) {
    return ControlMode::CLOSED_LOOP_MONITOR;
  }
  // SPEED_TRIM (also accepts legacy "PID", "CLOSED_LOOP_PID")
  if (strcmp(str, "SPEED_TRIM") == 0 || strcmp(str, "speed_trim") == 0 ||
      strcmp(str, "TRIM") == 0 || strcmp(str, "trim") == 0 ||
      strcmp(str, "CLOSED_LOOP_PID") == 0 || strcmp(str, "closed_loop_pid") == 0 ||
      strcmp(str, "PID") == 0 || strcmp(str, "pid") == 0 ||
      strcmp(str, "2") == 0) {
    return ControlMode::SPEED_TRIM;
  }
  return ControlMode::OPEN_LOOP;
}

const char *encoderStatusToString(EncoderStatus status) {
  switch (status) {
  case EncoderStatus::NOT_PRESENT:
    return "NOT_PRESENT";
  case EncoderStatus::INITIALIZING:
    return "INITIALIZING";
  case EncoderStatus::READY:
    return "READY";
  case EncoderStatus::FAULT:
    return "FAULT";
  default:
    return "UNKNOWN";
  }
}

bool ControlModeManager::init(ILock& lock) {
  m_lock = &lock;

  // Start in safe defaults
  m_currentMode = ControlMode::OPEN_LOOP;
  m_encoderStatus = EncoderStatus::NOT_PRESENT;

  return true;
}

bool ControlModeManager::setMode(ControlMode mode) {
  m_lock->acquire();

  bool success = false;

  if (mode == ControlMode::OPEN_LOOP) {
    // OPEN_LOOP is always allowed
    m_currentMode = mode;
    success = true;
  } else {
    // Both closed-loop modes require encoder to be ready
    if (m_encoderStatus == EncoderStatus::READY) {
      m_currentMode = mode;
      success = true;
    }
    // else: validation failed, don't change mode
  }

  m_lock->release();
  return success;
}

void ControlModeManager::setEncoderStatus(EncoderStatus status) {
  m_lock->acquire();

  m_encoderStatus = status;

  // If encoder becomes unavailable while in any closed-loop mode, revert
  if (status != EncoderStatus::READY &&
      m_currentMode != ControlMode::OPEN_LOOP) {
    m_currentMode = ControlMode::OPEN_LOOP;
  }

  m_lock->release();
}

} // namespace Services
