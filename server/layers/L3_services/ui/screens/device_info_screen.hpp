/**
 * @file device_info_screen.hpp
 * @brief Identity and health of this board: id, wheel role (editable), default mode, uptime,
 *        heap, CPU. CENTER (or RIGHT) edits the wheel; LEFT/RIGHT choose NONE, FL, FR, RL or
 *        RR; CENTER saves it to flash (the same store as DEV:ROLE); LEFT goes back otherwise.
 */

#ifndef DEVICE_INFO_SCREEN_HPP
#define DEVICE_INFO_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class DeviceInfoScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Device Info"; }
    const char* centerHint() const override { return m_editing ? "SAVE" : "EDIT"; }

private:
    bool m_needsRedraw = true;
    bool m_full = true;
    bool m_editing = false;
    bool m_wheelDirty = true;
    bool m_hintDirty = true;
    uint8_t m_role = 0;              ///< WheelRole value being shown / chosen
    char m_hint[40] = {};
    char m_uptime[16] = {};
    char m_heap[16] = {};
    char m_cpu[16] = {};

    void setHint(const char* text);
};

} // namespace UI

#endif // DEVICE_INFO_SCREEN_HPP
