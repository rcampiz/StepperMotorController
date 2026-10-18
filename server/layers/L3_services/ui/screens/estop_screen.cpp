/**
 * @file estop_screen.cpp
 * @brief E-STOP / FAULT page in the panel's look: neon hazard bands, an aberrated block title,
 *        a bracketed status panel, a segmented hold-to-clear bar and a red perspective grid.
 */

#include "ui/screens/estop_screen.hpp"
#include "ui/chrome/chrome.hpp"
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

constexpr uint16_t CELL = 6;                 // title block size: 30 x 42 glyphs
constexpr uint16_t BAND_H = 18;              // hazard bands top and bottom
constexpr uint16_t TITLE_Y = 32;
constexpr uint16_t PANEL_Y = 100, PANEL_H = 62;
constexpr uint16_t GRID_TOP = 246, GRID_BOTTOM = 297;
constexpr uint8_t SEGMENTS = 20;
// The middle content's eight positions: a small orbit, never more than 5 px from home
constexpr int8_t SHIFTS[8][2] = {{0, 0}, {4, 2}, {-3, 4}, {5, -3}, {-4, -2}, {2, 5}, {-5, 1}, {3, -4}};

void blockText(Services::ICanvas& lcd, const char* s, int16_t dx, uint16_t y, uint16_t colour)
{
    const size_t n = strlen(s);
    const auto w = static_cast<int16_t>(n * 6 * CELL - CELL);
    auto x = static_cast<int16_t>((Theme::WIDTH - w) / 2 + dx);
    for (size_t i = 0; i < n; i++, x = static_cast<int16_t>(x + 6 * CELL)) {
        const uint8_t* cols = nullptr;
        for (const Glyph& g : GLYPHS) if (g.c == s[i]) cols = g.cols;
        if (cols == nullptr) continue;
        for (uint8_t c = 0; c < 5; c++)
            for (uint8_t r = 0; r < 7; r++)
                if (cols[c] & (1u << r))
                    lcd.fillRect(static_cast<uint16_t>(x + c * CELL), static_cast<uint16_t>(y + r * CELL), CELL, CELL, colour);
    }
}

uint16_t mix565(uint16_t a, uint16_t b, int t256)
{
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return static_cast<uint16_t>(((ar + (br - ar) * t256 / 256) << 11) |
                                 ((ag + (bg - ag) * t256 / 256) << 5) | (ab + (bb - ab) * t256 / 256));
}

} // namespace

void EstopScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_progress = 0;
    m_message[0] = '\0';
    m_frames = 0;
    m_shift = 0;
    m_offX = m_offY = 0;
}

void EstopScreen::drawBands(Services::ICanvas& lcd)
{
    // Diagonal neon-yellow / page stripes, 12 px wide, crawling one pixel per frame
    static uint8_t row[Theme::WIDTH * 2];
    for (int band = 0; band < 2; band++) {
        const uint16_t y0 = band == 0 ? 0 : static_cast<uint16_t>(Theme::HEIGHT - BAND_H);
        lcd.streamBitmapStart(0, y0, Theme::WIDTH, BAND_H);
        for (uint16_t r = 0; r < BAND_H; r++) {
            for (uint16_t x = 0; x < Theme::WIDTH; x++) {
                const bool on = (((x + r + m_stripePhase) / 12) & 1u) == 0;
                const uint16_t c = on ? Theme::YELLOW : Theme::BG;
                row[x * 2] = static_cast<uint8_t>(c >> 8);
                row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
            }
            lcd.streamBitmapData(row, sizeof row);
        }
        lcd.streamBitmapEnd();
    }
}

void EstopScreen::drawGrid(Services::ICanvas& lcd)
{
    // A red floor like the saver's, scrolling toward the viewer
    static uint8_t row[Theme::WIDTH * 2];
    const uint16_t rows = GRID_BOTTOM - GRID_TOP;
    constexpr float K = 60.0f;
    lcd.streamBitmapStart(0, GRID_TOP, Theme::WIDTH, rows);
    for (uint16_t r = 0; r < rows; r++) {
        const uint16_t bg = mix565(0x3005 /* deep red-violet */, Theme::BG, r * 256 / rows);
        const uint16_t line = mix565(bg, Theme::RED, 90 + r * 166 / rows);
        bool horizontal = (r == 0);
        if (r >= 5) {
            const float depth = K / static_cast<float>(r);
            const int n = static_cast<int>(depth + m_gridPhase + 0.5f);
            const float off = K / (static_cast<float>(n) - m_gridPhase) - static_cast<float>(r);
            horizontal = n > 0 && off > -0.5f && off <= 0.5f;
        }
        for (uint16_t x = 0; x < Theme::WIDTH; x++) {
            const uint16_t c = horizontal ? (r == 0 ? Theme::MAGENTA : line) : bg;
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        const int span = 4 + r * 22 / rows;
        if (!horizontal && span >= 9) {
            for (int j = -12; j <= 12; j++) {
                const int x = Theme::WIDTH / 2 + j * span;
                if (x >= 0 && x < Theme::WIDTH) {
                    row[x * 2] = static_cast<uint8_t>(line >> 8);
                    row[x * 2 + 1] = static_cast<uint8_t>(line & 0xFF);
                }
            }
        }
        lcd.streamBitmapData(row, sizeof row);
    }
    lcd.streamBitmapEnd();
}

void EstopScreen::setMessage(const char* msg)
{
    snprintf(m_message, sizeof m_message, "%s", msg != nullptr ? msg : "");
}

void EstopScreen::drawStatic(Services::ICanvas& lcd)
{
    lcd.waitForVSync();
    lcd.fillScreen(Theme::BG);
    drawBands(lcd);
    lcd.fillRect(0, BAND_H, Theme::WIDTH, 2, Theme::MAGENTA);
    lcd.fillRect(0, static_cast<uint16_t>(Theme::HEIGHT - BAND_H - 2), Theme::WIDTH, 2, Theme::MAGENTA);
    const int16_t ox = m_offX, oy = m_offY;
    auto X = [&](int v) { return static_cast<uint16_t>(v + ox); };
    auto Y = [&](int v) { return static_cast<uint16_t>(v + oy); };

    // Title: cyan ghost right, magenta ghost left, red on top
    const char* title = m_fault ? "FAULT" : "E-STOP";
    blockText(lcd, title, static_cast<int16_t>(3 + ox), Y(TITLE_Y), Theme::CYAN);
    blockText(lcd, title, static_cast<int16_t>(-3 + ox), Y(TITLE_Y), Theme::MAGENTA);
    blockText(lcd, title, ox, Y(TITLE_Y), Theme::RED);
    lcd.blitTextLine(X(Theme::MARGIN), Y(82), Theme::WIDTH - 2 * Theme::MARGIN - 6, 8,
                     m_fault ? "     DRIVER FAULT  //  MOTOR SAFE" : "   EMERGENCY STOP  //  MOTOR SAFE",
                     Theme::DIM, Theme::BG);

    // Status panel, bracketed like the menu pages
    lcd.fillRect(X(Theme::MARGIN), Y(PANEL_Y), Theme::WIDTH - 2 * Theme::MARGIN, PANEL_H, Theme::PANEL);
    Chrome::drawCorners(lcd, X(Theme::MARGIN), Y(PANEL_Y), Theme::WIDTH - 2 * Theme::MARGIN, PANEL_H, 8, Theme::CYAN);
    lcd.blitTextLine(X(16), Y(PANEL_Y + 8), 50, 8, "MOTOR", Theme::CYAN, Theme::PANEL);
    lcd.drawString(X(70), Y(PANEL_Y + 6), "released (Hi-Z)", Theme::TEXT, Theme::PANEL, Theme::FONT_MEDIUM);
    lcd.blitTextLine(X(16), Y(PANEL_Y + 28), 50, 8, "DRIVER", Theme::CYAN, Theme::PANEL);
    lcd.blitTextLine(X(16), Y(PANEL_Y + 46), 50, 8, "LATCH", Theme::CYAN, Theme::PANEL);
    lcd.blitTextLine(X(70), Y(PANEL_Y + 46), 140, 8, m_fault ? "driver fault" : "E-STOP", Theme::MAGENTA, Theme::PANEL);

    // The way out
    lcd.drawString(X(28), Y(176), "HOLD CENTER 3s TO CLEAR", Theme::YELLOW, Theme::BG, Theme::FONT_MEDIUM);
    Chrome::drawCorners(lcd, X(BAR_X - 4), Y(BAR_Y - 4),
                        static_cast<uint16_t>(BAR_W + 8), static_cast<uint16_t>(BAR_H + 8), 6, Theme::GRID_MAJ);
    drawGrid(lcd);
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
        snprintf(line, sizeof line, "in standby");
    } else if (ocd || uvlo || thsd || stalled) {
        snprintf(line, sizeof line, "%s%s%s%s", ocd ? "OCD " : "", uvlo ? "UVLO " : "",
                 thsd ? "THERMAL " : "", stalled ? "STALL" : "");
    } else {
        snprintf(line, sizeof line, "no fault");
    }
    const bool bad = statusReg != 0 && (ocd || uvlo || thsd || stalled);
    lcd.blitTextLine(static_cast<uint16_t>(70 + m_offX), static_cast<uint16_t>(PANEL_Y + 28 + m_offY), 140, 8,
                     line, bad ? Theme::ORANGE : Theme::GREEN, Theme::PANEL);
}

void EstopScreen::render(Services::ICanvas& lcd)
{
    // Every SHIFT_FRAMES the middle moves to its next position (a full repaint)
    if (++m_frames >= SHIFT_FRAMES) {
        m_frames = 0;
        m_shift = static_cast<uint8_t>((m_shift + 1) % 8);
        m_offX = SHIFTS[m_shift][0];
        m_offY = SHIFTS[m_shift][1];
        m_full = true;
    }
    if (m_full) {
        m_full = false;
        drawStatic(lcd);
    } else {
        // The stripes crawl and the grid scrolls every frame
        m_stripePhase = static_cast<uint8_t>((m_stripePhase + 1) % 24);
        m_gridPhase += 0.06f;
        if (m_gridPhase >= 1.0f) m_gridPhase -= 1.0f;
        lcd.waitForVSync();
        drawBands(lcd);
        drawGrid(lcd);
    }
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    if (t.motor.statusReg != m_drawnStatus || t.motor.stalled != m_drawnStall) {
        m_drawnStatus = t.motor.statusReg;
        m_drawnStall = t.motor.stalled;
        drawFaults(lcd, t.motor.statusReg, t.motor.stalled);
    }
    if (m_progress != m_drawnProgress) {
        // Segments, filled left to right, cyan shading to magenta
        const uint8_t lit = static_cast<uint8_t>(m_progress * SEGMENTS / 100);
        const uint16_t seg = BAR_W / SEGMENTS;
        for (uint8_t i = 0; i < SEGMENTS; i++) {
            const uint16_t c = (i < lit) ? mix565(Theme::CYAN, Theme::MAGENTA, i * 256 / (SEGMENTS - 1)) : Theme::PANEL;
            lcd.fillRect(static_cast<uint16_t>(BAR_X + i * seg + m_offX), static_cast<uint16_t>(BAR_Y + m_offY),
                         static_cast<uint16_t>(seg - 2), BAR_H, c);
        }
        m_drawnProgress = m_progress;
    }
    if (strcmp(m_message, m_drawnMessage) != 0) {
        snprintf(m_drawnMessage, sizeof m_drawnMessage, "%s", m_message);
        lcd.blitTextLine(static_cast<uint16_t>(20 + m_offX), static_cast<uint16_t>(224 + m_offY), 196, 8,
                         m_message, Theme::ORANGE, Theme::BG);
    }
}

} // namespace UI
