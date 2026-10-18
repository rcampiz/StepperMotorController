/**
 * @file recovery_screen.cpp
 * @brief Retention recovery: full-panel colour cycling with a small countdown.
 */

#include "ui/screens/recovery_screen.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include <stdio.h>

namespace UI {

// Full-scale fields in turn: every sub-pixel gets driven hard both ways
static constexpr uint16_t FIELDS[] = { 0xFFFF, 0x0000, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F, 0x8410 };
static constexpr uint8_t FIELD_COUNT = 9;

void RecoveryScreen::onActivate()
{
    m_needsRedraw = true;
    m_framesLeft = static_cast<uint32_t>(m_minutes) * 60u * 20u;
    m_step = 0;
    m_frames = STEP_FRAMES;                        // paint the first field at once
}

void RecoveryScreen::render(Services::ICanvas& lcd)
{
    if (m_framesLeft == 0) return;
    m_framesLeft--;
    if (++m_frames < STEP_FRAMES) return;
    m_frames = 0;
    uint16_t field = FIELDS[m_step];
    m_step = static_cast<uint8_t>((m_step + 1) % FIELD_COUNT);
    lcd.waitForVSync();
    lcd.fillScreen(field);
    // A small countdown in a corner, in the opposite shade so it is legible on any field
    char buf[16];
    uint32_t secs = m_framesLeft / 20u;
    snprintf(buf, sizeof buf, "%lu:%02lu", static_cast<unsigned long>(secs / 60), static_cast<unsigned long>(secs % 60));
    uint16_t ink = (field == 0x0000 || field == 0x001F || field == 0xF800) ? 0xFFFF : 0x0000;
    lcd.drawString(4, static_cast<uint16_t>(Theme::HEIGHT - 10), buf, ink, field, Theme::FONT_SMALL);
}

InputResult RecoveryScreen::handleInput(JoyDirection dir, bool pressed)
{
    (void)dir;
    if (pressed) { m_framesLeft = 0; return InputResult::EXIT_SCREEN; }
    return InputResult::HANDLED;
}

} // namespace UI
