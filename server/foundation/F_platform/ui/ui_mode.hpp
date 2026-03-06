/**
 * @file ui_mode.hpp
 * @brief UI mode enum and manager — Foundation-level, no L3 dependencies
 *
 * Supports two operation modes:
 * - LOCAL: MCU owns UI state machine, joystick navigates locally
 * - REMOTE: Upstream controls display, joystick events forwarded
 *
 * Extracted from L3_services/ui/ui_mode.hpp so that L2 (command_parser)
 * can query/set the mode without an upward dependency.
 */

#ifndef F_PLATFORM_UI_MODE_HPP
#define F_PLATFORM_UI_MODE_HPP

#include "harness/pins/ilock.hpp"
#include "harness/pins/iui_mode.hpp"

namespace UI {

/**
 * @brief UI Mode Manager - thread-safe mode control
 */
class UIModeManager : public Harness::IUIModeProvider {
public:
    /**
     * @brief Initialize the UI mode manager
     * @param lock Lock for thread safety
     * @param defaultMode Initial mode (default: LOCAL)
     * @return true on success
     */
    bool init(Harness::ILock& lock, UIMode defaultMode = UIMode::LOCAL);

    /**
     * @brief Get current UI mode
     */
    UIMode getMode() const override;

    /**
     * @brief Set UI mode
     * @param mode New mode
     * @return true if mode changed successfully
     */
    bool setMode(UIMode mode) override;

    /**
     * @brief Set callback for joystick events in REMOTE mode
     */
    void setJoyEventCallback(JoyEventCallback callback);

    /**
     * @brief Report a joystick event
     *
     * In LOCAL mode: event is ignored (handled by display task)
     * In REMOTE mode: callback is invoked if set
     */
    void reportJoyEvent(const JoyEvent& event);

    /**
     * @brief Get mode name as string
     */
    static const char* modeName(UIMode mode);

    /**
     * @brief Get direction name as string
     */
    static const char* directionName(JoyDirection dir);

private:
    UIMode m_mode = UIMode::LOCAL;
    JoyEventCallback m_joyCallback = nullptr;
    Harness::ILock* m_lock = nullptr;
};

// Global UI mode manager instance
extern UIModeManager g_uiMode;

} // namespace UI

#endif // F_PLATFORM_UI_MODE_HPP
