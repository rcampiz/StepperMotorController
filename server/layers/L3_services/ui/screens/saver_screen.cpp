/**
 * @file saver_screen.cpp
 * @brief Hotline Miami screen saver: stripe band, aberrated title card, VHS glitches.
 */

#include "ui/screens/saver_screen.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"

namespace UI {

// 5x7 glyphs, one byte per column, bit 0 = top row (the driver's font shapes)
static constexpr uint8_t GLYPH_S[5] = { 0x46, 0x49, 0x49, 0x49, 0x31 };
static constexpr uint8_t GLYPH_M[5] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F };
static constexpr uint8_t GLYPH_C[5] = { 0x3E, 0x41, 0x41, 0x41, 0x22 };

// The stripe band: four colours cycling along the diagonal
static constexpr uint16_t STRIPES[4] = { Theme::MAGENTA, Theme::VIOLET, Theme::CYAN, 0x5010 /* deep purple */ };
static constexpr uint16_t STRIPE_W = 14;

void SaverScreen::onActivate()
{
    m_needsRedraw = true;       // the manager paints the panel black
    m_frames = HOLD_FRAMES;     // lay out on the first frame
    m_glitchFrames = 0;
    m_drawn = false;
    m_glitchDrawn = false;
}

uint32_t SaverScreen::nextRandom()
{
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return m_seed;
}

void SaverScreen::drawBand(Services::ICanvas& lcd, uint16_t y, uint8_t phase, bool erase)
{
    if (erase) {
        lcd.fillRect(0, y, Theme::WIDTH, BAND_H, Theme::BLACK);
        return;
    }
    // 45-degree stripes: on each row the pattern shifts one pixel; drawn as row segments
    for (uint16_t r = 0; r < BAND_H; r++) {
        auto yy = static_cast<uint16_t>(y + r);
        // first segment starts at a negative offset so the diagonal is continuous
        int16_t x = -static_cast<int16_t>((r + phase * STRIPE_W) % (4 * STRIPE_W));
        uint8_t c = 0;
        while (x < static_cast<int16_t>(Theme::WIDTH)) {
            int16_t x0 = x < 0 ? 0 : x;
            int16_t x1 = static_cast<int16_t>(x + STRIPE_W);
            if (x1 > static_cast<int16_t>(Theme::WIDTH)) x1 = Theme::WIDTH;
            if (x1 > x0) lcd.fillRect(static_cast<uint16_t>(x0), yy, static_cast<uint16_t>(x1 - x0), 1, STRIPES[c]);
            x = static_cast<int16_t>(x + STRIPE_W);
            c = static_cast<uint8_t>((c + 1) & 3);
        }
    }
    // Hard edges above and below, the VHS way: a bright line and a dark gap
    lcd.fillRect(0, static_cast<uint16_t>(y - 2), Theme::WIDTH, 1, Theme::CYAN);
    lcd.fillRect(0, static_cast<uint16_t>(y + BAND_H + 1), Theme::WIDTH, 1, Theme::MAGENTA);
}

void SaverScreen::drawGlyph(Services::ICanvas& lcd, const uint8_t* glyph, uint16_t x, uint16_t y, uint16_t color)
{
    for (uint8_t col = 0; col < 5; col++) {
        for (uint8_t row = 0; row < 7; row++) {
            if (glyph[col] & (1u << row)) {
                lcd.fillRect(static_cast<uint16_t>(x + col * CELL), static_cast<uint16_t>(y + row * CELL), CELL, CELL, color);
            }
        }
    }
}

void SaverScreen::drawCard(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase)
{
    if (erase) {
        lcd.fillRect(x, y, CARD_W, CARD_H, Theme::BLACK);
        return;
    }
    const uint8_t* glyphs[3] = { GLYPH_S, GLYPH_M, GLYPH_C };
    // Chromatic aberration: magenta copy shifted left, cyan copy shifted right, white on top
    for (uint8_t i = 0; i < 3; i++) {
        auto gx = static_cast<uint16_t>(x + 6 + i * 6 * CELL);
        drawGlyph(lcd, glyphs[i], static_cast<uint16_t>(gx - 3), static_cast<uint16_t>(y + 3), Theme::MAGENTA);
        drawGlyph(lcd, glyphs[i], static_cast<uint16_t>(gx + 3), static_cast<uint16_t>(y + 3), Theme::CYAN);
    }
    for (uint8_t i = 0; i < 3; i++) {
        auto gx = static_cast<uint16_t>(x + 6 + i * 6 * CELL);
        drawGlyph(lcd, glyphs[i], gx, static_cast<uint16_t>(y + 3), Theme::TEXT);
    }
}

void SaverScreen::layout(Services::ICanvas& lcd)
{
    if (m_glitchDrawn) {
        lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 3, Theme::BLACK);
        m_glitchDrawn = false;
    }
    if (m_drawn) {
        drawCard(lcd, m_cardX, m_cardY, true);
        drawBand(lcd, m_bandY, m_bandPhase, true);
        lcd.fillRect(0, static_cast<uint16_t>(m_bandY - 2), Theme::WIDTH, 1, Theme::BLACK);
        lcd.fillRect(0, static_cast<uint16_t>(m_bandY + BAND_H + 1), Theme::WIDTH, 1, Theme::BLACK);
    }
    // The band somewhere in the upper or lower third, the card clear of it
    m_bandY = static_cast<uint16_t>(8 + nextRandom() % (Theme::HEIGHT - BAND_H - 16));
    m_bandPhase = static_cast<uint8_t>(nextRandom() & 3);
    uint16_t above = m_bandY > CARD_H + 10 ? static_cast<uint16_t>(m_bandY - CARD_H - 8) : 0;
    uint16_t below = static_cast<uint16_t>(Theme::HEIGHT - (m_bandY + BAND_H) > CARD_H + 10
                                           ? Theme::HEIGHT - (m_bandY + BAND_H) - CARD_H - 8 : 0);
    if (above >= below) {
        m_cardY = static_cast<uint16_t>(2 + (above ? nextRandom() % above : 0));
    } else {
        m_cardY = static_cast<uint16_t>(m_bandY + BAND_H + 6 + nextRandom() % below);
    }
    m_cardX = static_cast<uint16_t>(nextRandom() % (Theme::WIDTH - CARD_W));
    drawBand(lcd, m_bandY, m_bandPhase, false);
    drawCard(lcd, m_cardX, m_cardY, false);
    m_drawn = true;
}

void SaverScreen::glitch(Services::ICanvas& lcd)
{
    // A VHS slice: a short bright bar at a random height, gone on the next glitch
    if (m_glitchDrawn) {
        lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 3, Theme::BLACK);
        m_glitchDrawn = false;
    }
    if ((nextRandom() & 3) == 0) return;             // sometimes nothing, so it does not tick
    m_glitchW = static_cast<uint16_t>(30 + nextRandom() % 120);
    m_glitchX = static_cast<uint16_t>(nextRandom() % (Theme::WIDTH - m_glitchW));
    m_glitchY = static_cast<uint16_t>(nextRandom() % (Theme::HEIGHT - 3));
    // Never over the band or the card: those are repainted on the next layout anyway,
    // but a slice through them would leave a hole until then
    if (m_glitchY + 3 > m_bandY - 2 && m_glitchY < m_bandY + BAND_H + 2) return;
    if (m_glitchY + 3 > m_cardY && m_glitchY < m_cardY + CARD_H) return;
    lcd.fillRect(m_glitchX, m_glitchY, m_glitchW, 1, Theme::CYAN);
    lcd.fillRect(m_glitchX, static_cast<uint16_t>(m_glitchY + 1), m_glitchW, 1, Theme::TEXT);
    lcd.fillRect(m_glitchX, static_cast<uint16_t>(m_glitchY + 2), m_glitchW, 1, Theme::MAGENTA);
    m_glitchDrawn = true;
}

void SaverScreen::render(Services::ICanvas& lcd)
{
    if (m_settings != nullptr && m_settings->saverStyle == SaverStyle::BLANK) {
        return;                                    // black, nothing moves, nothing is sent
    }
    if (++m_frames >= HOLD_FRAMES) {
        m_frames = 0;
        layout(lcd);
        return;
    }
    if (++m_glitchFrames >= GLITCH_FRAMES) {
        m_glitchFrames = 0;
        glitch(lcd);
    }
}

InputResult SaverScreen::handleInput(JoyDirection dir, bool pressed)
{
    (void)dir;
    return pressed ? InputResult::EXIT_SCREEN : InputResult::HANDLED;
}

} // namespace UI
