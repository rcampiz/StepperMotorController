/**
 * @file control_mode.hpp
 * @brief Control mode management with encoder status tracking
 *
 * Manages runtime control mode state:
 *   - OPEN_LOOP: Motion by step count only, no encoder feedback
 *   - CLOSED_LOOP: Encoder verification (requires encoder hardware)
 *
 * Also tracks encoder hardware availability for mode validation.
 */

#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace Services {

/**
 * @brief Control mode for motion execution
 */
enum class ControlMode : uint8_t {
    OPEN_LOOP   = 0,  // Step-based motion, no encoder feedback
    CLOSED_LOOP = 1   // Encoder verification (not correction in v1)
};

/**
 * @brief Encoder hardware status
 */
enum class EncoderStatus : uint8_t {
    NOT_PRESENT  = 0,  // Encoder not detected or init failed
    INITIALIZING = 1,  // Encoder init in progress
    READY        = 2,  // Encoder operational
    FAULT        = 3   // Encoder error detected
};

/**
 * @brief Convert ControlMode to string
 */
const char* modeToString(ControlMode mode);

/**
 * @brief Parse ControlMode from string
 * @return OPEN_LOOP if invalid
 */
ControlMode parseMode(const char* str);

/**
 * @brief Convert EncoderStatus to string
 */
const char* encoderStatusToString(EncoderStatus status);

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
     * CLOSED_LOOP mode requires encoder to be READY.
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
    bool canEnterClosedLoop() const {
        return isEncoderAvailable();
    }

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
