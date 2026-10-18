/**
 * @file saver_screen.cpp
 * @brief Screen saver rendering.
 */

#include "ui/screens/saver_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"

namespace UI {

void SaverScreen::onActivate()
{
    m_needsRedraw = true;       // the manager paints the panel black
    m_frames = HOLD_FRAMES;     // place the mark on the first frame
    m_drawn = false;
}

uint32_t SaverScreen::nextRandom()
{
    // xorshift32: enough to wander the mark around the panel
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return m_seed;
}

void SaverScreen::drawMark(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase)
{
    if (erase) {
        lcd.fillRect(x, y, MARK_W, MARK_H, Theme::BLACK);
        return;
    }
    Chrome::drawCorners(lcd, x, y, MARK_W, MARK_H, 8, Theme::GRID_MAJ);
    lcd.drawString(static_cast<uint16_t>(x + 10), static_cast<uint16_t>(y + 10), "SMC",
                   Theme::CYAN, Theme::BLACK, Theme::FONT_LARGE);
    lcd.fillRect(static_cast<uint16_t>(x + 10), static_cast<uint16_t>(y + 28), 36, 1, Theme::MAGENTA);
}

void SaverScreen::render(Services::ICanvas& lcd)
{
    if (m_settings != nullptr && m_settings->saverStyle == SaverStyle::BLANK) {
        return;                                    // black, nothing moves, nothing is sent
    }
    if (++m_frames < HOLD_FRAMES) return;
    m_frames = 0;
    if (m_drawn) drawMark(lcd, m_x, m_y, true);
    m_x = static_cast<uint16_t>(nextRandom() % (Theme::WIDTH - MARK_W));
    m_y = static_cast<uint16_t>(nextRandom() % (Theme::HEIGHT - MARK_H));
    drawMark(lcd, m_x, m_y, false);
    m_drawn = true;
}

InputResult SaverScreen::handleInput(JoyDirection dir, bool pressed)
{
    (void)dir;
    return pressed ? InputResult::EXIT_SCREEN : InputResult::HANDLED;
}

} // namespace UI
