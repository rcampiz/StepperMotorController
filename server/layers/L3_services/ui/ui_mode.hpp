/**
 * @file ui_mode.hpp
 * @brief UI mode management for dual-mode LCD system
 *
 * Supports two operation modes:
 * - LOCAL: MCU owns UI state machine, joystick navigates locally
 * - REMOTE: Upstream controls display, joystick events forwarded
 */

#ifndef UI_MODE_HPP
#define UI_MODE_HPP

#include "F_platform/interfaces/ilock.hpp"
#include <stdint.h>

namespace UI {

/**
 * @brief UI operation mode
 */
enum class UIMode : uint8_t {
    LOCAL,   ///< MCU owns UI state machine
    REMOTE   ///< Upstream controls display content
};

/**
 * @brief Screen types for local mode
 */
enum class ScreenType : uint8_t {
    STATUS,    ///< Telemetry display (existing pages)
    MENU,      ///< List-based navigation
    TERMINAL,  ///< Scrolling text console
    IMAGE,     ///< Full-screen bitmap
    CANVAS     ///< Remote-driven arbitrary rendering
};

/**
 * @brief Joystick direction for event forwarding
 */
enum class JoyDirection : uint8_t {
    NONE = 0,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    CENTER
};

/**
 * @brief Joystick event structure
 */
struct JoyEvent {
    JoyDirection direction;
    bool pressed;          ///< true = pressed, false = released
    uint32_t timestamp;    ///< Tick count when event occurred
};

/**
 * @brief Callback type for joystick events in REMOTE mode
 */
using JoyEventCallback = void(*)(const JoyEvent& event);

/**
 * @brief UI Mode Manager - thread-safe mode control
 */
class UIModeManager {
public:
    /**
     * @brief Initialize the UI mode manager
     * @param defaultMode Initial mode (default: LOCAL)
     * @return true on success
     */
    bool init(ILock& lock, UIMode defaultMode = UIMode::LOCAL);

    /**
     * @brief Get current UI mode
     * @return Current mode
     */
    UIMode getMode() const;

    /**
     * @brief Set UI mode
     * @param mode New mode
     * @return true if mode changed successfully
     */
    bool setMode(UIMode mode);

    /**
     * @brief Set callback for joystick events in REMOTE mode
     * @param callback Function to call on joystick events
     */
    void setJoyEventCallback(JoyEventCallback callback);

    /**
     * @brief Report a joystick event
     *
     * In LOCAL mode: event is ignored (handled by display task)
     * In REMOTE mode: callback is invoked if set
     *
     * @param event Joystick event to report
     */
    void reportJoyEvent(const JoyEvent& event);

    /**
     * @brief Get mode name as string
     * @param mode Mode to convert
     * @return String representation ("LOCAL" or "REMOTE")
     */
    static const char* modeName(UIMode mode);

    /**
     * @brief Get direction name as string
     * @param dir Direction to convert
     * @return String representation
     */
    static const char* directionName(JoyDirection dir);

private:
    UIMode m_mode = UIMode::LOCAL;
    JoyEventCallback m_joyCallback = nullptr;
    ILock* m_lock = nullptr;
};

// Global UI mode manager instance
extern UIModeManager g_uiMode;

} // namespace UI

#endif // UI_MODE_HPP
