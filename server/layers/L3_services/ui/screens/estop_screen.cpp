/**
 * @file estop_screen.cpp
 * @brief E-STOP / FAULT page: hazard bands, a block title, what is latched, hold-to-clear.
 */

#include "ui/screens/estop_screen.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

namespace {

// 5x7 glyphs, one byte per column, bit 0 = top row
struct Glyph { char c; uint8_t cols[5]; };
constexpr Glyph GLYPHS[] = {
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
};

constexpr uint16_t HAZARD_A = 0xFF20;   // Theme::YELLOW
constexpr uint16_t HAZARD_B = 0x0000;
constexpr uint16_t TITLE    = 0xF906;   // Theme::RED
constexpr uint16_t CELL = 6;            // block size of the title glyphs

void blockText(Services::ICanvas& lcd, const char* s, uint16_t y, uint16_t colour)
{
    const size_t n = strlen(s);
    const uint16_t w = static_cast<uint16_t>(n * 6 * CELL - CELL);
    uint16_t x = static_cast<uint16_t>((Theme::WIDTH - w) / 2);
    for (size_t i = 0; i < n; i++, x = static_cast<uint16_t>(x + 6 * CELL)) {
        const uint8_t* cols = nullptr;
        for (const Glyph& g : GLYPHS) if (g.c == s[i]) cols = g.cols;
        if (cols == nullptr) continue;
        for (uint8_t c = 0; c < 5; c++)
            for (uint8_t r = 0; r < 7; r++)
                if (cols[c] & (1u << r))
                    lcd.fillRect(static_cast<uint16_t>(x + c * CELL), static_cast<uint16_t>(y + r * CELL), CELL, CELL, colour);
    }
}

void hazardBand(Services::ICanvas& lcd, uint16_t y, uint16_t h)
{
    // Diagonal yellow/black stripes, 12 px wide, one row segment at a time
    for (uint16_t r = 0; r < h; r++) {
        int16_t x = static_cast<int16_t>(-static_cast<int16_t>(r % 24));
        bool yellow = true;
        while (x < static_cast<int16_t>(Theme::WIDTH)) {
            int16_t x0 = x < 0 ? 0 : x;
            int16_t x1 = static_cast<int16_t>(x + 12);
            if (x1 > static_cast<int16_t>(Theme::WIDTH)) x1 = Theme::WIDTH;
            if (x1 > x0) lcd.fillRect(static_cast<uint16_t>(x0), static_cast<uint16_t>(y + r),
                                      static_cast<uint16_t>(x1 - x0), 1, yellow ? HAZARD_A : HAZARD_B);
            x = static_cast<int16_t>(x + 12);
            yellow = !yellow;
        }
    }
}

} // namespace

void EstopScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_progress = 0;
    m_message[0] = '\0';
}

void EstopScreen::setMessage(const char* msg)
{
    snprintf(m_message, sizeof m_message, "%s", msg != nullptr ? msg : "");
}

void EstopScreen::drawStatic(Services::ICanvas& lcd)
{
    lcd.fillScreen(Theme::BLACK);
    hazardBand(lcd, 0, 22);
    hazardBand(lcd, Theme::HEIGHT - 22, 22);
    blockText(lcd, m_fault ? "FAULT" : "E-STOP", 44, TITLE);
    lcd.drawString(20, 104, "motor released (Hi-Z)", Theme::TEXT, Theme::BLACK, Theme::FONT_MEDIUM);
    lcd.drawString(20, 206, "hold CENTER 3s to clear", HAZARD_A, Theme::BLACK, Theme::FONT_MEDIUM);
    lcd.drawRect(static_cast<uint16_t>(BAR_X - 2), static_cast<uint16_t>(BAR_Y - 2),
                 static_cast<uint16_t>(BAR_W + 4), static_cast<uint16_t>(BAR_H + 4), Theme::GRID_MAJ);
    m_drawnProgress = 0xFF;
    m_drawnMessage[0] = '\x01';
    m_drawnStatus = 0xFFFF;
}

void EstopScreen::drawFaults(Services::ICanvas& lcd, uint16_t statusReg, bool stalled)
{
    char line[40];
    const bool ocd = !(statusReg & (1u << 13));
    const bool uvlo = !(statusReg & (1u << 9));
    const bool thsd = ((statusReg >> 11) & 0x3u) >= 2;
    if (statusReg == 0) {
        snprintf(line, sizeof line, "driver: in standby");
    } else if (ocd || uvlo || thsd || stalled) {
        snprintf(line, sizeof line, "driver:%s%s%s%s", ocd ? " OCD" : "", uvlo ? " UVLO" : "",
                 thsd ? " THERMAL" : "", stalled ? " STALL" : "");
    } else {
        snprintf(line, sizeof line, "driver: no fault");
    }
    const bool bad = statusReg != 0 && (ocd || uvlo || thsd || stalled);
    lcd.blitTextLine(20, 130, 200, 8, line, bad ? Theme::ORANGE : Theme::GREEN, Theme::BLACK);
    lcd.blitTextLine(20, 146, 200, 8, m_fault ? "latched by a driver fault" : "latched: E-STOP",
                     Theme::DIM, Theme::BLACK);
}

void EstopScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        drawStatic(lcd);
    }
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    if (t.motor.statusReg != m_drawnStatus || t.motor.stalled != m_drawnStall) {
        m_drawnStatus = t.motor.statusReg;
        m_drawnStall = t.motor.stalled;
        drawFaults(lcd, t.motor.statusReg, t.motor.stalled);
    }
    if (m_progress != m_drawnProgress) {
        const uint16_t fill = static_cast<uint16_t>(BAR_W * m_progress / 100);
        if (fill > 0) lcd.fillRect(BAR_X, BAR_Y, fill, BAR_H, HAZARD_A);
        if (fill < BAR_W) lcd.fillRect(static_cast<uint16_t>(BAR_X + fill), BAR_Y,
                                       static_cast<uint16_t>(BAR_W - fill), BAR_H, Theme::PANEL);
        m_drawnProgress = m_progress;
    }
    if (strcmp(m_message, m_drawnMessage) != 0) {
        snprintf(m_drawnMessage, sizeof m_drawnMessage, "%s", m_message);
        lcd.blitTextLine(20, 262, 200, 8, m_message, Theme::ORANGE, Theme::BLACK);
    }
}

} // namespace UI
