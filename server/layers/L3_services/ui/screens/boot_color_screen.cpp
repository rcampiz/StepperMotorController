/**
 * @file boot_color_screen.cpp
 * @brief Boot color / SMPTE test pattern screen implementation
 */

#include "ui/screens/boot_color_screen.hpp"
#include "harness/pins/icanvas.hpp"

namespace UI {

void BootColorScreen::onActivate()
{
    m_drawn = false;
}

void BootColorScreen::render(Harness::ICanvas& lcd)
{
    if (!m_drawn) {
        lcd.drawTestPattern();
        m_drawn = true;
    }
}

InputResult BootColorScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (pressed && dir != JoyDirection::NONE) {
        return InputResult::EXIT_SCREEN;
    }
    return InputResult::HANDLED;
}

} // namespace UI
