/**
 * @file graph_screen.cpp
 * @brief Telemetry strip chart with a major/minor grid and per-column diff rendering.
 */

#include "ui/screens/graph_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "ui/screens/encoder_screen.hpp"      // ENCODER_CPR
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t HEADER_Y = Theme::CONTENT_Y + 2;      // channel + latest value
static constexpr uint16_t LABEL_X = 2;                           // y-axis labels (small font)
static constexpr uint16_t HINT_Y = 270;

static inline void setPx(uint8_t* buf, uint16_t row, uint16_t color)
{
    buf[row * 2] = static_cast<uint8_t>(color >> 8);
    buf[row * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
}

void GraphScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    // Samples are kept across visits; everything on the panel is redrawn
}

void GraphScreen::resetColumns()
{
    memset(m_lo, 0xFF, sizeof m_lo);
    memset(m_hi, 0xFF, sizeof m_hi);
}

void GraphScreen::pushSample(int32_t value)
{
    m_samples[m_head] = value;
    m_head = static_cast<uint16_t>((m_head + 1) % SAMPLES);
    if (m_count < SAMPLES) m_count++;
}

int32_t GraphScreen::getSample(uint16_t age) const
{
    if (age >= m_count) return 0;
    auto idx = static_cast<uint16_t>((m_head + SAMPLES - 1 - age) % SAMPLES);
    return m_samples[idx];
}

const char* GraphScreen::channelName() const
{
    switch (m_channel) {
        case Channel::VELOCITY: return "Shaft rpm";
        case Channel::SPEED:    return "Cmd speed";
        case Channel::POSITION: return "Position";
        case Channel::SLIP:     return "Slip";
    }
    return "?";
}

const char* GraphScreen::channelUnit() const
{
    switch (m_channel) {
        case Channel::VELOCITY: return "rpm";
        case Channel::SPEED:    return "st/s";
        case Channel::POSITION: return "st";
        case Channel::SLIP:     return "cnt";
    }
    return "";
}

uint16_t GraphScreen::channelColor() const
{
    switch (m_channel) {
        case Channel::VELOCITY: return Theme::CYAN;
        case Channel::SPEED:    return Theme::MAGENTA;
        case Channel::POSITION: return Theme::YELLOW;
        case Channel::SLIP:     return Theme::ORANGE;
    }
    return Theme::TEXT;
}

void GraphScreen::dataRange(int32_t& outMin, int32_t& outMax) const
{
    uint16_t n = (m_count < PLOT_W) ? m_count : PLOT_W;
    outMin = 0; outMax = 0;
    for (uint16_t i = 0; i < n; i++) {
        int32_t v = getSample(i);
        if (v < outMin) outMin = v;
        if (v > outMax) outMax = v;
    }
    if (outMin > -10) outMin = -10;
    if (outMax < 10) outMax = 10;
}

void GraphScreen::updateRange()
{
    if (m_yScaleMode == YScaleMode::FIXED) return;
    int32_t lo, hi;
    dataRange(lo, hi);
    // Grow when the data leaves the drawn range; shrink only when it uses under half of it.
    // Each range change moves every column, so hysteresis keeps the plot still.
    bool grow = lo < m_min || hi > m_max;
    int32_t span = m_max - m_min;
    bool shrink = (hi - lo) * 2 < span && span > 40;
    if (!grow && !shrink) return;
    int32_t pad = (hi - lo) / 5;
    if (pad < 2) pad = 2;
    m_min = lo - pad;
    m_max = hi + pad;
}

uint16_t GraphScreen::rowOf(int32_t value) const
{
    int32_t span = m_max - m_min;
    if (span <= 0) span = 1;
    int64_t h = static_cast<int64_t>(value - m_min) * (PLOT_H - 1) / span;
    if (h < 0) h = 0;
    if (h > PLOT_H - 1) h = PLOT_H - 1;
    return static_cast<uint16_t>(PLOT_H - 1 - h);
}

void GraphScreen::drawFrame(Services::ICanvas& lcd)
{
    // The plot interior: black, minor grid every 10 px, major every 50 px, drawn column-wise
    for (uint16_t x = 0; x < PLOT_W; x++) {
        bool majX = (x % GRID_MAJOR) == 0, minX = (x % GRID_MINOR) == 0;
        for (uint16_t y = 0; y < PLOT_H; y++) {
            bool majY = (y % GRID_MAJOR) == 0, minY = (y % GRID_MINOR) == 0;
            uint16_t c = Theme::BLACK;
            if (majX || majY) c = Theme::GRID_MAJ;
            else if (minX || minY) c = Theme::GRID;
            setPx(m_colBuf, y, c);
        }
        lcd.blitColumn(static_cast<uint16_t>(PLOT_X + x), PLOT_Y, PLOT_H, m_colBuf);
    }
    Chrome::drawCorners(lcd, static_cast<uint16_t>(PLOT_X - 2), static_cast<uint16_t>(PLOT_Y - 2),
                        static_cast<uint16_t>(PLOT_W + 4), static_cast<uint16_t>(PLOT_H + 4), 8, Theme::CYAN);
    // Time labels under the plot
    lcd.drawString(PLOT_X, static_cast<uint16_t>(PLOT_Y + PLOT_H + 5), "-10s", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
    lcd.drawString(static_cast<uint16_t>(PLOT_X + PLOT_W / 2 - 9), static_cast<uint16_t>(PLOT_Y + PLOT_H + 5), "-5s", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
    lcd.drawString(static_cast<uint16_t>(PLOT_X + PLOT_W - 12), static_cast<uint16_t>(PLOT_Y + PLOT_H + 5), "0s", Theme::DIM, Theme::BG, Theme::FONT_SMALL);
    Chrome::hintLine(lcd, HINT_Y, "CENTER channel   UP/DOWN lock scale", Theme::DIM);
    Chrome::drawDeck(lcd, static_cast<uint16_t>(HINT_Y + 12), Theme::CONTENT_BOTTOM);
    m_rangeText[0][0] = m_rangeText[1][0] = '\0';
    resetColumns();
}

void GraphScreen::drawAxisLabels(Services::ICanvas& lcd)
{
    char buf[12];
    const char* lock = (m_yScaleMode == YScaleMode::FIXED) ? "LOCK" : "    ";
    snprintf(buf, sizeof buf, "%5ld", static_cast<long>(m_max));
    if (strcmp(buf, m_rangeText[0]) != 0) {
        snprintf(m_rangeText[0], sizeof m_rangeText[0], "%s", buf);
        lcd.blitTextLine(LABEL_X, PLOT_Y, 30, Theme::SMALL_H, buf, Theme::DIM, Theme::BG);
        lcd.blitTextLine(LABEL_X, static_cast<uint16_t>(PLOT_Y + PLOT_H / 2 - 4), 30, Theme::SMALL_H, lock,
                         Theme::YELLOW, Theme::BG);
    }
    snprintf(buf, sizeof buf, "%5ld", static_cast<long>(m_min));
    if (strcmp(buf, m_rangeText[1]) != 0) {
        snprintf(m_rangeText[1], sizeof m_rangeText[1], "%s", buf);
        lcd.blitTextLine(LABEL_X, static_cast<uint16_t>(PLOT_Y + PLOT_H - Theme::SMALL_H), 30, Theme::SMALL_H, buf, Theme::DIM, Theme::BG);
    }
}

void GraphScreen::drawHeader(Services::ICanvas& lcd, bool force)
{
    char buf[20];
    if (force) {
        Chrome::textBox(lcd, Theme::MARGIN, HEADER_Y, 110, 16, 2, 2, channelName(), channelColor(), Theme::BG);
        m_valueText[0] = '\0';
    }
    int32_t latest = (m_count > 0) ? getSample(0) : 0;
    snprintf(buf, sizeof buf, "%ld %s", static_cast<long>(latest), channelUnit());
    if (strcmp(buf, m_valueText) != 0) {
        snprintf(m_valueText, sizeof m_valueText, "%s", buf);
        size_t n = strlen(buf);
        if (n > 13) n = 13;
        auto tw = static_cast<uint16_t>(n * Theme::MED_W);
        Chrome::textBox(lcd, 122, HEADER_Y, 112, 16, static_cast<uint16_t>(112 - tw - 2), 2, buf, Theme::TEXT, Theme::BG);
    }
}

void GraphScreen::drawColumns(Services::ICanvas& lcd)
{
    uint16_t shown = (m_count < PLOT_W) ? m_count : PLOT_W;
    const uint16_t color = channelColor();
    uint16_t prevRow = 0;
    bool havePrev = false;

    for (uint16_t x = 0; x < PLOT_W; x++) {
        auto age = static_cast<uint16_t>(PLOT_W - 1 - x);
        uint8_t lo = 0xFF, hi = 0xFF;
        if (age < shown) {
            uint16_t row = rowOf(getSample(age));
            if (havePrev) {
                lo = static_cast<uint8_t>(row < prevRow ? row : prevRow);
                hi = static_cast<uint8_t>(row > prevRow ? row : prevRow);
            } else {
                lo = hi = static_cast<uint8_t>(row);
            }
            prevRow = row;
            havePrev = true;
        }
        if (lo == m_lo[x] && hi == m_hi[x]) continue;        // this column is already right

        // Repaint the union of the old span and the new one, grid restored, trace on top
        uint16_t from = PLOT_H, to = 0;
        if (m_lo[x] != 0xFF) { from = m_lo[x]; to = m_hi[x]; }
        if (lo != 0xFF) { if (lo < from) from = lo; if (hi > to) to = hi; }
        if (from > to) { m_lo[x] = lo; m_hi[x] = hi; continue; }
        bool majX = (x % GRID_MAJOR) == 0, minX = (x % GRID_MINOR) == 0;
        for (uint16_t y = from; y <= to; y++) {
            bool majY = (y % GRID_MAJOR) == 0, minY = (y % GRID_MINOR) == 0;
            uint16_t c = Theme::BLACK;
            if (majX || majY) c = Theme::GRID_MAJ;
            else if (minX || minY) c = Theme::GRID;
            if (lo != 0xFF && y >= lo && y <= hi) c = color;
            setPx(m_colBuf, static_cast<uint16_t>(y - from), c);
        }
        lcd.blitColumn(static_cast<uint16_t>(PLOT_X + x), static_cast<uint16_t>(PLOT_Y + from),
                       static_cast<uint16_t>(to - from + 1), m_colBuf);
        m_lo[x] = lo;
        m_hi[x] = hi;
    }
}

void GraphScreen::render(Services::ICanvas& lcd)
{
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    int32_t value = 0;
    switch (m_channel) {
        case Channel::VELOCITY: value = static_cast<int32_t>(static_cast<int64_t>(t.encoder.velocity) * 60 / ENCODER_CPR); break;
        case Channel::SPEED:    value = static_cast<int32_t>(t.motor.speed); break;
        case Channel::POSITION: value = t.motor.position; break;
        case Channel::SLIP:     value = t.control.followingError; break;
    }
    pushSample(value);

    bool full = m_full;
    if (full) {
        m_full = false;
        drawFrame(lcd);
    }
    updateRange();
    drawAxisLabels(lcd);
    drawHeader(lcd, full);
    drawColumns(lcd);
}

InputResult GraphScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    switch (dir) {
        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;
        case JoyDirection::CENTER:
        case JoyDirection::RIGHT: {
            auto ch = static_cast<uint8_t>((static_cast<uint8_t>(m_channel) + 1) % NUM_CHANNELS);
            m_channel = static_cast<Channel>(ch);
            m_count = 0;
            m_head = 0;
            m_yScaleMode = YScaleMode::DYNAMIC;
            m_min = -10; m_max = 10;
            m_full = true;                               // new channel: fresh frame
            return InputResult::HANDLED;
        }
        case JoyDirection::UP:
        case JoyDirection::DOWN:
            m_yScaleMode = (m_yScaleMode == YScaleMode::DYNAMIC) ? YScaleMode::FIXED : YScaleMode::DYNAMIC;
            m_rangeText[0][0] = '\0';                    // relabel with the lock mark
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
