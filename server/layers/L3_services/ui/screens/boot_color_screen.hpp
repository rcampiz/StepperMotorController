/**
 * @file boot_color_screen.hpp
 * @brief Boot color / SMPTE test pattern screen
 *
 * Displays the LCD test pattern on activate.
 * Any button press exits back to the menu.
 *
 * No RTOS headers. No driver dependencies beyond LCD.
 */

#ifndef BOOT_COLOR_SCREEN_HPP
#define BOOT_COLOR_SCREEN_HPP

#include "ui/screen.hpp"

namespace UI {

class BootColorScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::IMAGE; }
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

private:
    bool m_drawn = false;
};

} // namespace UI

#endif // BOOT_COLOR_SCREEN_HPP
