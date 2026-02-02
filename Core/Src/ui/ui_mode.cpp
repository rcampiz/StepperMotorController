/**
 * @file ui_mode.cpp
 * @brief UI mode manager implementation
 */

#include "ui/ui_mode.hpp"

namespace UI {

// Global instance
UIModeManager g_uiMode;

bool UIModeManager::init(UIMode defaultMode) {
    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        return false;
    }
    m_mode = defaultMode;
    m_joyCallback = nullptr;
    return true;
}

UIMode UIModeManager::getMode() const {
    return m_mode;
}

bool UIModeManager::setMode(UIMode mode) {
    if (m_mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m_mode = mode;
        xSemaphoreGive(m_mutex);
        return true;
    }
    return false;
}

void UIModeManager::setJoyEventCallback(JoyEventCallback callback) {
    if (m_mutex != nullptr && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m_joyCallback = callback;
        xSemaphoreGive(m_mutex);
    }
}

void UIModeManager::reportJoyEvent(const JoyEvent& event) {
    // Only forward events in REMOTE mode
    if (m_mode != UIMode::REMOTE) {
        return;
    }

    JoyEventCallback cb = nullptr;
    if (m_mutex != nullptr && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        cb = m_joyCallback;
        xSemaphoreGive(m_mutex);
    }

    if (cb != nullptr) {
        cb(event);
    }
}

const char* UIModeManager::modeName(UIMode mode) {
    switch (mode) {
        case UIMode::LOCAL:  return "LOCAL";
        case UIMode::REMOTE: return "REMOTE";
        default:             return "UNKNOWN";
    }
}

const char* UIModeManager::directionName(JoyDirection dir) {
    switch (dir) {
        case JoyDirection::NONE:   return "NONE";
        case JoyDirection::LEFT:   return "LEFT";
        case JoyDirection::RIGHT:  return "RIGHT";
        case JoyDirection::UP:     return "UP";
        case JoyDirection::DOWN:   return "DOWN";
        case JoyDirection::CENTER: return "CENTER";
        default:                   return "UNKNOWN";
    }
}

} // namespace UI
