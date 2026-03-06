/**
 * @file device_info_screen.hpp
 * @brief Device info / persistent memory viewer screen
 *
 * Displays device identity and system information:
 *   - Device ID, Wheel Role, Default Mode
 *   - Uptime, Free Heap
 *
 * Reads from DeviceConfigManager and TelemetryManager via
 * their public APIs. No RTOS headers.
 */

#ifndef DEVICE_INFO_SCREEN_HPP
#define DEVICE_INFO_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class DeviceInfoScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    bool m_needsRedraw = true;
};

} // namespace UI

#endif // DEVICE_INFO_SCREEN_HPP
