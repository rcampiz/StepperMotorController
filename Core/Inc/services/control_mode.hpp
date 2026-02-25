/**
 * @file control_mode.hpp
 * @brief Control mode management with encoder status tracking
 *
 * Manages runtime control mode state:
 *   - OPEN_LOOP:            No encoder feedback (step-based only)
 *   - CLOSED_LOOP_MONITOR:  Encoder detects slip, Tier 2/3 response (no trim)
 *   - SPEED_TRIM:           PI speed-trim correction (final = base + trim)
 *
 * Also tracks encoder hardware availability for mode validation.
 */

#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>

namespace Services {

/**
 * @brief Control mode for motion execution
 */
enum class ControlMode : uint8_t {
  OPEN_LOOP            = 0,  // Step-based motion, no encoder feedback
  CLOSED_LOOP_MONITOR  = 1,  // Encoder slip detection, fault response only
  SPEED_TRIM           = 2,  // PI speed-trim correction (final = base + trim)
  // Legacy alias — existing code referencing CLOSED_LOOP_PID still compiles
  CLOSED_LOOP_PID      = SPEED_TRIM
};

/**
 * @brief Encoder hardware status
 */
enum class EncoderStatus : uint8_t {
  NOT_PRESENT = 0,  // Encoder not detected or init failed
  INITIALIZING = 1, // Encoder init in progress
  READY = 2,        // Encoder operational
  FAULT = 3         // Encoder error detected
};

/**
 * @brief Convert ControlMode to string
 */
const char *modeToString(ControlMode mode);

/**
 * @brief Parse ControlMode from string
 * @return OPEN_LOOP if invalid
 */
ControlMode parseMode(const char *str);

/**
 * @brief Convert EncoderStatus to string
 */
const char *encoderStatusToString(EncoderStatus status);

/**
 * @brief Control mode manager
 *
 * Thread-safe singleton managing runtime control mode and encoder status.
 */
class ControlModeManager {
public:
  /**
   * @brief Initialize control mode manager
   *
   * Starts in OPEN_LOOP mode with encoder NOT_PRESENT.
   *
   * @return true on success
   */
  bool init();

  /**
   * @brief Get current control mode
   */
  ControlMode getMode() const { return m_currentMode; }

  /**
   * @brief Set control mode
   *
   * CLOSED_LOOP_MONITOR and SPEED_TRIM require encoder to be READY.
   *
   * @param mode Desired control mode
   * @return true if mode change succeeded
   */
  bool setMode(ControlMode mode);

  /**
   * @brief Get encoder hardware status
   */
  EncoderStatus getEncoderStatus() const { return m_encoderStatus; }

  /**
   * @brief Set encoder hardware status
   *
   * Called by encoder task during initialization and on errors.
   * If encoder becomes unavailable, automatically reverts to OPEN_LOOP.
   *
   * @param status New encoder status
   */
  void setEncoderStatus(EncoderStatus status);

  /**
   * @brief Check if encoder is available for closed-loop operation
   */
  bool isEncoderAvailable() const {
    return m_encoderStatus == EncoderStatus::READY;
  }

  /**
   * @brief Check if closed-loop mode can be entered
   *
   * Returns true if encoder is READY.
   */
  bool canEnterClosedLoop() const { return isEncoderAvailable(); }

private:
  ControlMode m_currentMode;
  EncoderStatus m_encoderStatus;
  SemaphoreHandle_t m_mutex;

  bool lock(TickType_t timeout = portMAX_DELAY);
  void unlock();
};

// Global instance
extern ControlModeManager g_controlMode;

} // namespace Services
