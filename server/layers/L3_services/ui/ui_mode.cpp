/**
 * @file ui_mode.cpp
 * @brief UI mode manager implementation
 */

#include "ui/ui_mode.hpp"

namespace UI {

// Global instance
UIModeManager g_uiMode;

bool UIModeManager::init(ILock& lock, UIMode defaultMode) {
    m_lock = &lock;
    m_mode = defaultMode;
    m_joyCallback = nullptr;
    return true;
}

UIMode UIModeManager::getMode() const {
    return m_mode;
}

bool UIModeManager::setMode(UIMode mode) {
    if (m_lock == nullptr) {
        return false;
    }

    m_lock->acquire();
    m_mode = mode;
    m_lock->release();
    return true;
}

void UIModeManager::setJoyEventCallback(JoyEventCallback callback) {
    if (m_lock != nullptr) {
        m_lock->acquire();
        m_joyCallback = callback;
        m_lock->release();
    }
}

void UIModeManager::reportJoyEvent(const JoyEvent& event) {
    // Only forward events in REMOTE mode
    if (m_mode != UIMode::REMOTE) {
        return;
    }

    JoyEventCallback cb = nullptr;
    if (m_lock != nullptr) {
        m_lock->acquire();
        cb = m_joyCallback;
        m_lock->release();
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
