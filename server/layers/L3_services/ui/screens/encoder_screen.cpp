/**
 * @file encoder_screen.cpp
 * @brief Encoder dial, value cells and the sweeping velocity scope.
 */

#include "ui/screens/encoder_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "graphics/trig_lut/trig_lut.hpp"
#include "graphics/primitives/primitives.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

// Value cells: two columns of three, under the dial
static constexpr uint16_t CELLS_Y = 190;
static constexpr uint16_t CELL_H = 18;
static constexpr uint16_t CELL_LABEL_W = 28;
static constexpr uint16_t CELL_W = 112;

void EncoderScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_prevAngle1024 = 0xFFFF;
    m_prevIndexSeen = false;
    memset(m_cell, 0, sizeof m_cell);
    m_scopeCol = 0;
    m_scopeScale = 1000;
}

void EncoderScreen::i64toa(int64_t val, char* buf, int bufSize)
{
    if (bufSize < 2) { buf[0] = '\0'; return; }
    bool neg = (val < 0);
    if (neg) val = -val;
    char tmp[24];
    int len = 0;
    if (val == 0) tmp[len++] = '0';
    while (val > 0 && len < 22) { tmp[len++] = static_cast<char>('0' + (val % 10)); val /= 10; }
    if (neg && len < 22) tmp[len++] = '-';
    int out = 0;
    for (int i = len - 1; i >= 0 && out < bufSize - 1; i--) buf[out++] = tmp[i];
    buf[out] = '\0';
}

// Point at radius r and angle1024 (0 = top, clockwise) from the dial centre
static void dialPoint(int16_t r, uint16_t angle1024, int16_t& x, int16_t& y)
{
    int16_t s = Graphics::sin_q15(angle1024), c = Graphics::cos_q15(angle1024);
    x = static_cast<int16_t>(120 + ((static_cast<int32_t>(s) * r) >> 15));
    y = static_cast<int16_t>(116 - ((static_cast<int32_t>(c) * r) >> 15));
}

void EncoderScreen::drawStatic(Services::ICanvas& lcd)
{
    // Rings: a faint outer halo and the cyan ring
    Graphics::drawCircleOutline(lcd, DIAL_CX, DIAL_CY, static_cast<int16_t>(DIAL_R + 5), Theme::GRID_MAJ);
    Graphics::fillRing(lcd, DIAL_CX, DIAL_CY, DIAL_R, static_cast<int16_t>(DIAL_R - 2), Theme::CYAN);
    // Ticks: every 10 degrees, the quadrants longer and magenta
    for (uint16_t deg = 0; deg < 360; deg = static_cast<uint16_t>(deg + 10)) {
        bool major = (deg % 90) == 0;
        uint16_t a = Graphics::deg_to_angle1024(deg);
        int16_t x0, y0, x1, y1;
        dialPoint(static_cast<int16_t>(DIAL_R - (major ? 14 : 7)), a, x0, y0);
        dialPoint(static_cast<int16_t>(DIAL_R - 3), a, x1, y1);
        lcd.drawLine(x0, y0, x1, y1,
                     major ? Theme::MAGENTA : Theme::VIOLET);
    }
    // Quadrant labels outside the ring
    lcd.drawString(static_cast<uint16_t>(DIAL_CX + DIAL_R + 10), static_cast<uint16_t>(DIAL_CY - 4), "90", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
    lcd.drawString(static_cast<uint16_t>(DIAL_CX - DIAL_R - 26), static_cast<uint16_t>(DIAL_CY - 4), "270", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
    // Index mark above the top tick: outline until the index pulse has been seen
    Graphics::drawCircleOutline(lcd, DIAL_CX, static_cast<int16_t>(DIAL_CY - DIAL_R - 10), 3, Theme::YELLOW);
    // Hub
    Graphics::fillCircle(lcd, DIAL_CX, DIAL_CY, 5, Theme::MAGENTA);
    Graphics::fillCircle(lcd, DIAL_CX, DIAL_CY, 2, Theme::TEXT);

    // Cell labels
    static const char* const LABELS[6] = {"POS", "REV", "VEL", "RPM", "IDX", "ERR"};
    for (uint8_t i = 0; i < 6; i++) {
        auto x = static_cast<uint16_t>(Theme::MARGIN + (i % 2) * (CELL_W + 4));
        auto y = static_cast<uint16_t>(CELLS_Y + (i / 2) * CELL_H);
        lcd.drawString(static_cast<uint16_t>(x + 2), static_cast<uint16_t>(y + 4), LABELS[i],
                       (i == 2 || i == 3) ? Theme::MAGENTA : (i == 4 ? Theme::YELLOW : Theme::CYAN),
                       Theme::BG, Theme::FONT_SMALL);
    }

    // Scope frame: black interior with a mid line, brackets, label
    lcd.fillRect(SCOPE_X, SCOPE_Y, SCOPE_W, SCOPE_H, Theme::BLACK);
    lcd.fillRect(SCOPE_X, static_cast<uint16_t>(SCOPE_Y + SCOPE_H / 2), SCOPE_W, 1, Theme::GRID);
    Chrome::drawCorners(lcd, static_cast<uint16_t>(SCOPE_X - 2), static_cast<uint16_t>(SCOPE_Y - 2),
                        static_cast<uint16_t>(SCOPE_W + 4), static_cast<uint16_t>(SCOPE_H + 4), 8, Theme::GRID_MAJ);
    lcd.drawString(static_cast<uint16_t>(SCOPE_X + 2), static_cast<uint16_t>(SCOPE_Y + SCOPE_H + 4),
                   "VEL  11 s sweep", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
}

void EncoderScreen::drawNeedle(Services::ICanvas& lcd, uint16_t angle1024, uint16_t color)
{
    int16_t tipX, tipY, baseX, baseY;
    dialPoint(NEEDLE_LEN, angle1024, tipX, tipY);
    dialPoint(7, angle1024, baseX, baseY);
    lcd.drawLine(baseX, baseY, tipX, tipY, color);
    lcd.drawLine(static_cast<int16_t>(baseX + 1), baseY, static_cast<int16_t>(tipX + 1), tipY, color);
}

void EncoderScreen::drawCell(Services::ICanvas& lcd, uint8_t i, const char* label, const char* value, uint16_t color)
{
    (void)label;
    if (strncmp(value, m_cell[i], sizeof m_cell[i]) == 0) return;
    snprintf(m_cell[i], sizeof m_cell[i], "%s", value);
    auto x = static_cast<uint16_t>(Theme::MARGIN + (i % 2) * (CELL_W + 4) + CELL_LABEL_W);
    auto y = static_cast<uint16_t>(CELLS_Y + (i / 2) * CELL_H);
    Chrome::textBox(lcd, x, y, static_cast<uint16_t>(CELL_W - CELL_LABEL_W), 16, 0, 2, value, color, Theme::BG);
}

void EncoderScreen::drawScopeColumn(Services::ICanvas& lcd, int32_t velocity)
{
    int32_t mag = velocity < 0 ? -velocity : velocity;
    while (mag > m_scopeScale && m_scopeScale < 2000000) m_scopeScale *= 2;   // sticky, never shrinks

    const uint16_t mid = SCOPE_H / 2;
    int32_t h = velocity * static_cast<int32_t>(mid - 1) / m_scopeScale;
    auto row = static_cast<uint16_t>(mid - h);
    if (row >= SCOPE_H) row = SCOPE_H - 1;

    auto set = [&](uint16_t r, uint16_t colour) {
        m_colBuf[r * 2] = static_cast<uint8_t>(colour >> 8);
        m_colBuf[r * 2 + 1] = static_cast<uint8_t>(colour & 0xFF);
    };
    for (uint16_t r = 0; r < SCOPE_H; r++) set(r, Theme::BLACK);
    set(mid, Theme::GRID);
    // Fill from the mid line to the value, the tip brighter
    uint16_t lo = row < mid ? row : mid, hi = row < mid ? mid : row;
    for (uint16_t r = lo; r <= hi; r++) set(r, Theme::GRID_MAJ);
    set(row, Theme::CYAN);
    lcd.blitColumn(static_cast<uint16_t>(SCOPE_X + m_scopeCol), SCOPE_Y, SCOPE_H, m_colBuf);
    // Cursor: a magenta column just ahead, erased as the sweep reaches it
    auto next = static_cast<uint16_t>((m_scopeCol + 1) % SCOPE_W);
    for (uint16_t r = 0; r < SCOPE_H; r++) set(r, Theme::MAGENTA);
    lcd.blitColumn(static_cast<uint16_t>(SCOPE_X + next), SCOPE_Y, SCOPE_H, m_colBuf);
    m_scopeCol = next;
}

void EncoderScreen::render(Services::ICanvas& lcd)
{
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    if (m_full) {
        m_full = false;
        drawStatic(lcd);
    }

    // Angle from the count: degrees x10 for the text, 1024ths for the needle
    auto countMod = static_cast<int32_t>(t.encoder.count % ENCODER_CPR);
    if (countMod < 0) countMod += ENCODER_CPR;
    auto angle1024 = static_cast<uint16_t>((static_cast<int64_t>(countMod) * 1024) / ENCODER_CPR);
    auto degX10 = static_cast<int32_t>((static_cast<int64_t>(countMod) * 3600) / ENCODER_CPR);

    if (angle1024 != m_prevAngle1024) {
        if (m_prevAngle1024 != 0xFFFF) drawNeedle(lcd, m_prevAngle1024, Theme::BG);
        drawNeedle(lcd, angle1024, Theme::MAGENTA);
        Graphics::fillCircle(lcd, DIAL_CX, DIAL_CY, 2, Theme::TEXT);
        m_prevAngle1024 = angle1024;
        char buf[12];
        snprintf(buf, sizeof buf, "%ld.%ld", static_cast<long>(degX10 / 10), static_cast<long>(degX10 % 10));
        // Angle readout inside the dial, centred under the hub, always 5 cells wide
        char padded[8];
        snprintf(padded, sizeof padded, "%5s", buf);
        lcd.drawString(static_cast<uint16_t>(DIAL_CX - 20), static_cast<uint16_t>(DIAL_CY + 20), padded,
                       Theme::TEXT, Theme::BG, Theme::FONT_MEDIUM);
        lcd.drawString(static_cast<uint16_t>(DIAL_CX + 21), static_cast<uint16_t>(DIAL_CY + 19), "o",
                       Theme::TEXT, Theme::BG, Theme::FONT_SMALL);
    }
    if (t.encoder.indexSeen && !m_prevIndexSeen) {
        Graphics::fillCircle(lcd, DIAL_CX, static_cast<int16_t>(DIAL_CY - DIAL_R - 10), 3, Theme::YELLOW);
        m_prevIndexSeen = true;
    }

    // Cells
    char buf[16], tmp[20];
    i64toa(t.encoder.count, tmp, sizeof tmp);
    snprintf(buf, sizeof buf, "%s", tmp);
    drawCell(lcd, 0, "POS", buf, Theme::TEXT);
    auto revX1000 = static_cast<int32_t>((t.encoder.count * 1000LL) / ENCODER_CPR);
    bool neg = revX1000 < 0; if (neg) revX1000 = -revX1000;
    snprintf(buf, sizeof buf, "%s%ld.%03ld", neg ? "-" : "", static_cast<long>(revX1000 / 1000), static_cast<long>(revX1000 % 1000));
    drawCell(lcd, 1, "REV", buf, Theme::TEXT);
    snprintf(buf, sizeof buf, "%ld c/s", static_cast<long>(t.encoder.velocity));
    drawCell(lcd, 2, "VEL", buf, Theme::TEXT);
    int32_t rpmX10 = static_cast<int32_t>((static_cast<int64_t>(t.encoder.velocity) * 600) / ENCODER_CPR);
    neg = rpmX10 < 0; if (neg) rpmX10 = -rpmX10;
    snprintf(buf, sizeof buf, "%s%ld.%ld", neg ? "-" : "", static_cast<long>(rpmX10 / 10), static_cast<long>(rpmX10 % 10));
    drawCell(lcd, 3, "RPM", buf, Theme::TEXT);
    if (t.encoder.indexPeriodUs > 0) snprintf(buf, sizeof buf, "%lu us", static_cast<unsigned long>(t.encoder.indexPeriodUs));
    else snprintf(buf, sizeof buf, "%s", t.encoder.indexSeen ? "seen" : "---");
    drawCell(lcd, 4, "IDX", buf, Theme::TEXT);
    snprintf(buf, sizeof buf, "%ld", static_cast<long>(t.control.followingError));
    drawCell(lcd, 5, "ERR", buf, (t.control.followingError > 50 || t.control.followingError < -50) ? Theme::ORANGE : Theme::TEXT);

    drawScopeColumn(lcd, t.encoder.velocity);
}

InputResult EncoderScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    if (dir == JoyDirection::LEFT) return InputResult::EXIT_SCREEN;
    return InputResult::HANDLED;
}

} // namespace UI
