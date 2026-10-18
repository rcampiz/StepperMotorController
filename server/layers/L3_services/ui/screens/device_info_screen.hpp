/**
 * @file device_info_screen.hpp
 * @brief Identity and health of this board: id, wheel role, default mode, uptime, heap, CPU.
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
    const char* centerHint() const override { return nullptr; }

private:
    bool m_needsRedraw = true;
    bool m_full = true;
    char m_uptime[16] = {};
    char m_heap[16] = {};
    char m_cpu[16] = {};
};

} // namespace UI

#endif // DEVICE_INFO_SCREEN_HPP
