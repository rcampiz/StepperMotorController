/**
 * @file control_mode.cpp
 * @brief Control mode manager implementation
 */

#include "services/control_mode.hpp"
#include <cstring>

namespace Services {

// Global instance
ControlModeManager g_controlMode;

const char* modeToString(ControlMode mode)
{
    switch (mode) {
        case ControlMode::OPEN_LOOP:   return "OPEN_LOOP";
        case ControlMode::CLOSED_LOOP: return "CLOSED_LOOP";
        default:                       return "UNKNOWN";
    }
}

ControlMode parseMode(const char* str)
{
    if (strcmp(str, "OPEN_LOOP") == 0 || strcmp(str, "open_loop") == 0 ||
        strcmp(str, "OPEN") == 0 || strcmp(str, "open") == 0 ||
        strcmp(str, "0") == 0) {
        return ControlMode::OPEN_LOOP;
    }
    if (strcmp(str, "CLOSED_LOOP") == 0 || strcmp(str, "closed_loop") == 0 ||
        strcmp(str, "CLOSED") == 0 || strcmp(str, "closed") == 0 ||
        strcmp(str, "1") == 0) {
        return ControlMode::CLOSED_LOOP;
    }
    return ControlMode::OPEN_LOOP;
}

const char* encoderStatusToString(EncoderStatus status)
{
    switch (status) {
        case EncoderStatus::NOT_PRESENT:  return "NOT_PRESENT";
        case EncoderStatus::INITIALIZING: return "INITIALIZING";
        case EncoderStatus::READY:        return "READY";
        case EncoderStatus::FAULT:        return "FAULT";
        default:                          return "UNKNOWN";
    }
}

bool ControlModeManager::init()
{
    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        return false;
    }

    // Start in safe defaults
    m_currentMode = ControlMode::OPEN_LOOP;
    m_encoderStatus = EncoderStatus::NOT_PRESENT;

    return true;
}

bool ControlModeManager::lock(TickType_t timeout)
{
    return xSemaphoreTake(m_mutex, timeout) == pdTRUE;
}

void ControlModeManager::unlock()
{
    xSemaphoreGive(m_mutex);
}

bool ControlModeManager::setMode(ControlMode mode)
{
    if (!lock()) {
        return false;
    }

    bool success = false;

    if (mode == ControlMode::OPEN_LOOP) {
        // OPEN_LOOP is always allowed
        m_currentMode = mode;
        success = true;
    }
    else if (mode == ControlMode::CLOSED_LOOP) {
        // CLOSED_LOOP requires encoder to be ready
        if (m_encoderStatus == EncoderStatus::READY) {
            m_currentMode = mode;
            success = true;
        }
        // else: validation failed, don't change mode
    }

    unlock();
    return success;
}

void ControlModeManager::setEncoderStatus(EncoderStatus status)
{
    if (!lock(pdMS_TO_TICKS(100))) {
        // If we can't get lock, at least update status
        m_encoderStatus = status;
        return;
    }

    m_encoderStatus = status;

    // If encoder becomes unavailable while in CLOSED_LOOP, revert to OPEN_LOOP
    if (status != EncoderStatus::READY &&
        m_currentMode == ControlMode::CLOSED_LOOP) {
        m_currentMode = ControlMode::OPEN_LOOP;
    }

    unlock();
}

} // namespace Services
