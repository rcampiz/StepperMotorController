/**
 * @file ui_mode.hpp
 * @brief UI mode manager (LOCAL / REMOTE) and joystick event forwarding — L3
 *
 * LOCAL: the DisplayService renders screens and consumes the joystick.
 * REMOTE: the host draws through the display dispatcher; joystick edges are forwarded
 * upstream through the registered callback (CommsService -> L2 emitJoystick).
 */
#ifndef L3_UI_MODE_HPP
#define L3_UI_MODE_HPP

#include "harness/pins/ui_types.hpp"
#include "F_platform/boundary/ilock.hpp"

namespace UI {

class UIModeManager {
public:
    bool init(Platform::ILock& lock, UIMode defaultMode = UIMode::LOCAL);

    UIMode getMode() const;
    bool setMode(UIMode mode);

    void setJoyEventCallback(JoyEventCallback callback);
    void reportJoyEvent(const JoyEvent& event);

    static const char* modeName(UIMode mode);
    static const char* directionName(JoyDirection dir);

private:
    UIMode m_mode = UIMode::LOCAL;
    JoyEventCallback m_joyCallback = nullptr;
    Platform::ILock* m_lock = nullptr;
};

extern UIModeManager g_uiMode;

} // namespace UI

#endif // L3_UI_MODE_HPP
