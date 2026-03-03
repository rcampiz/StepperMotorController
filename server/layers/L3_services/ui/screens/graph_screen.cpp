/**
 * @file graph_screen.cpp
 * @brief Rolling telemetry line graph — flicker-free column-buffer rendering
 *
 * Each graph column is built as a complete pixel buffer in RAM, then sent
 * to the LCD in a single DMA transfer via blitColumn(). No fillRect clear,
 * no per-pixel drawLine — zero visual flicker.
 */

#include "ui/screens/graph_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "F_platform/types/telemetry.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

// Encoder CPR for RPM conversion (must match encoder_screen.hpp and GUI)
static constexpr int32_t ENCODER_CPR = 4000;

// Colors (RGB565 big-endian for column buffer)
static constexpr uint16_t LABEL_COLOR = 0x07FF;   // Cyan
static constexpr uint16_t BG_COLOR = 0x0000;       // Black
static constexpr uint16_t GRID_COLOR = 0x4208;     // Dark gray
static constexpr uint16_t VALUE_COLOR = 0xFFFF;    // White
static constexpr uint16_t VEL_COLOR = 0x07E0;      // Green
static constexpr uint16_t SPD_COLOR = 0x07FF;      // Cyan
static constexpr uint16_t POS_COLOR = 0xFBE0;      // Yellow
static constexpr uint16_t SLIP_COLOR = 0xF81F;     // Magenta
static constexpr uint16_t AXIS_COLOR = 0x8410;     // Medium gray

// Layout
static constexpr uint8_t TEXT_SCALE = 3;   // Medium font (8×12)
static constexpr uint16_t HEADER_Y = 2;

// Helper: write a pixel color into the column buffer at a given row
static inline void setColPixel(uint8_t* buf, uint16_t row, uint16_t color)
{
    uint16_t off = row * 2;
    buf[off]     = static_cast<uint8_t>(color >> 8);
    buf[off + 1] = static_cast<uint8_t>(color & 0xFF);
}

void GraphScreen::onActivate()
{
    m_needsRedraw = true;
    // Don't clear samples — preserve history across menu visits
}

void GraphScreen::pushSample(int32_t value)
{
    m_samples[m_head] = value;
    m_head = (m_head + 1) % GRAPH_SAMPLES;
    if (m_count < GRAPH_SAMPLES) m_count++;
}

int32_t GraphScreen::getSample(uint16_t age) const
{
    if (age >= m_count) return 0;
    uint16_t idx = (m_head + GRAPH_SAMPLES - 1 - age) % GRAPH_SAMPLES;
    return m_samples[idx];
}

void GraphScreen::computeRange(int32_t& outMin, int32_t& outMax) const
{
    uint16_t n = (m_count < GRAPH_W) ? m_count : GRAPH_W;
    if (n == 0) {
        outMin = -1; outMax = 1;
        return;
    }
    outMin = getSample(0);
    outMax = outMin;
    for (uint16_t i = 1; i < n; i++) {
        int32_t v = getSample(i);
        if (v < outMin) outMin = v;
        if (v > outMax) outMax = v;
    }
    // Always include zero for absolute reference
    if (outMin > 0) outMin = 0;
    if (outMax < 0) outMax = 0;
    // Enforce minimum range of -10 to +10
    if (outMin > -10) outMin = -10;
    if (outMax < 10) outMax = 10;
    // Add 10% padding beyond extremes
    int32_t span = outMax - outMin;
    int32_t pad = span / 10;
    if (pad < 1) pad = 1;
    outMin -= pad;
    outMax += pad;
}

const char* GraphScreen::channelName() const
{
    switch (m_channel) {
        case Channel::VELOCITY: return "RPM";
        case Channel::SPEED:    return "Speed";
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
        case Channel::POSITION: return "steps";
        case Channel::SLIP:     return "ticks";
    }
    return "";
}

uint16_t GraphScreen::channelColor() const
{
    switch (m_channel) {
        case Channel::VELOCITY: return VEL_COLOR;
        case Channel::SPEED:    return SPD_COLOR;
        case Channel::POSITION: return POS_COLOR;
        case Channel::SLIP:     return SLIP_COLOR;
    }
    return VALUE_COLOR;
}

void GraphScreen::renderHeader(LCD& lcd)
{
    char buf[30];

    // Channel name with arrows — padded to fixed width, no fillRect clear
    snprintf(buf, sizeof(buf), "< %-8s >", channelName());
    lcd.drawString(4, HEADER_Y, buf, channelColor(), BG_COLOR, TEXT_SCALE);

    // Current value + unit — right-aligned, padded to overwrite old text
    int32_t latest = (m_count > 0) ? getSample(0) : 0;
    snprintf(buf, sizeof(buf), "%6ld %-5s", static_cast<long>(latest), channelUnit());
    lcd.drawString(120, HEADER_Y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
}

void GraphScreen::renderGraph(LCD& lcd)
{
    uint16_t displaySamples = (m_count < GRAPH_W) ? m_count : GRAPH_W;

    if (displaySamples < 2) {
        // Not enough data — clear graph area and show message
        lcd.fillRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, BG_COLOR);
        lcd.drawString(GRAPH_X + 20, GRAPH_Y + GRAPH_H / 2,
                       "No data", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
        return;
    }

    // Compute Y range (always includes zero for absolute reference)
    int32_t minVal, maxVal;
    if (m_yScaleMode == YScaleMode::FIXED) {
        minVal = m_fixedMin;
        maxVal = m_fixedMax;
    } else {
        computeRange(minVal, maxVal);
    }

    int32_t range = maxVal - minVal;
    uint16_t lineColor = channelColor();

    // Zero-line row (always in range since 0 is always included)
    int32_t zp = ((-minVal) * static_cast<int32_t>(GRAPH_H - 1)) / range;
    auto zeroRow = static_cast<uint16_t>(GRAPH_H - 1 - zp);

    // Column-buffer rendering: build each column in RAM, blit via DMA
    uint16_t prevPixelRow = 0;
    bool havePrev = false;

    for (uint16_t col = 0; col < GRAPH_W; col++) {
        // Fill entire column with background
        for (uint16_t row = 0; row < GRAPH_H; row++) {
            setColPixel(m_colBuf, row, BG_COLOR);
        }

        // Draw zero line (always visible)
        if (zeroRow < GRAPH_H) {
            setColPixel(m_colBuf, zeroRow, GRID_COLOR);
        }

        // Check if this column has data
        auto age = static_cast<uint16_t>(GRAPH_W - 1 - col);
        if (age < displaySamples) {
            int32_t val = getSample(age);

            // Map value to pixel row (0 = top, GRAPH_H-1 = bottom)
            int32_t pixelH = ((val - minVal) * static_cast<int32_t>(GRAPH_H - 1)) / range;
            if (pixelH < 0) pixelH = 0;
            if (pixelH >= static_cast<int32_t>(GRAPH_H)) pixelH = GRAPH_H - 1;
            auto curRow = static_cast<uint16_t>(GRAPH_H - 1 - pixelH);

            if (havePrev) {
                // Fill connected line segment from prevRow to curRow (2px thick)
                uint16_t rMin = (prevPixelRow < curRow) ? prevPixelRow : curRow;
                uint16_t rMax = (prevPixelRow > curRow) ? prevPixelRow : curRow;
                // Extend by 1 for 2px thickness
                uint16_t rEnd = (rMax + 1 < GRAPH_H) ? static_cast<uint16_t>(rMax + 1) : rMax;
                for (uint16_t r = rMin; r <= rEnd; r++) {
                    setColPixel(m_colBuf, r, lineColor);
                }
            } else {
                // First data point — draw 2px dot
                setColPixel(m_colBuf, curRow, lineColor);
                if (curRow + 1 < GRAPH_H) {
                    setColPixel(m_colBuf, static_cast<uint16_t>(curRow + 1), lineColor);
                }
            }

            prevPixelRow = curRow;
            havePrev = true;
        }

        // Blit column to LCD — single DMA transfer, no flicker
        lcd.blitColumn(static_cast<uint16_t>(GRAPH_X + col), GRAPH_Y, GRAPH_H, m_colBuf);
    }

    // Y-axis labels (scale=3, overlaid on left margin — padded to overwrite old text)
    char buf[12];
    snprintf(buf, sizeof(buf), "%-6ld", static_cast<long>(maxVal));
    lcd.drawString(2, GRAPH_Y + 2, buf, AXIS_COLOR, BG_COLOR, TEXT_SCALE);

    snprintf(buf, sizeof(buf), "%-6ld", static_cast<long>(minVal));
    lcd.drawString(2, static_cast<uint16_t>(GRAPH_Y + GRAPH_H - 14), buf, AXIS_COLOR, BG_COLOR, TEXT_SCALE);

    // Mode indicator in Y-axis margin (midway)
    const char* modeStr = (m_yScaleMode == YScaleMode::DYNAMIC) ? "Auto " : "Lock ";
    lcd.drawString(2, static_cast<uint16_t>(GRAPH_Y + (GRAPH_H / 2) - 6),
                   modeStr, AXIS_COLOR, BG_COLOR, TEXT_SCALE);

    // X-axis time labels (scale=3, below graph — padded)
    auto xAxisY = static_cast<uint16_t>(GRAPH_Y + GRAPH_H + 4);

    // Dynamic time window: displaySamples * 50ms per sample
    uint16_t totalMs = displaySamples * 50;
    uint16_t dataStartCol = (displaySamples < GRAPH_W)
        ? static_cast<uint16_t>(GRAPH_W - displaySamples) : 0;

    // Oldest time label (left edge of data)
    if (totalMs >= 1000) {
        snprintf(buf, sizeof(buf), "%-4us", totalMs / 1000);
    } else {
        snprintf(buf, sizeof(buf), "0.%us ", totalMs / 100);
    }
    lcd.drawString(static_cast<uint16_t>(GRAPH_X + dataStartCol), xAxisY,
                   buf, AXIS_COLOR, BG_COLOR, TEXT_SCALE);

    // Middle time label
    uint16_t halfMs = totalMs / 2;
    auto midCol = static_cast<uint16_t>(dataStartCol + (GRAPH_W - dataStartCol) / 2);
    if (halfMs >= 1000) {
        snprintf(buf, sizeof(buf), "%us", halfMs / 1000);
    } else {
        snprintf(buf, sizeof(buf), "0.%us", halfMs / 100);
    }
    auto midW = static_cast<uint16_t>(strlen(buf)) * 8;
    lcd.drawString(static_cast<uint16_t>(GRAPH_X + midCol - midW / 2), xAxisY,
                   buf, AXIS_COLOR, BG_COLOR, TEXT_SCALE);

    // "0s" at right (newest)
    lcd.drawString(static_cast<uint16_t>(GRAPH_X + GRAPH_W - 16), xAxisY,
                   "0s", AXIS_COLOR, BG_COLOR, TEXT_SCALE);
}

void GraphScreen::render(LCD& lcd)
{
    // On first frame after activation, clear entire screen once
    if (m_needsRedraw) {
        lcd.fillScreen(BG_COLOR);
    }

    // Sample current data
    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();

    int32_t value = 0;
    switch (m_channel) {
        case Channel::VELOCITY:
            value = telem.encoder.velocity * 60L / ENCODER_CPR;
            break;
        case Channel::SPEED:
            value = static_cast<int32_t>(telem.motor.speed);
            break;
        case Channel::POSITION:
            value = telem.motor.position;
            break;
        case Channel::SLIP:
            value = telem.control.followingError;
            break;
    }
    pushSample(value);

    renderHeader(lcd);
    renderGraph(lcd);
}

InputResult GraphScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    switch (dir) {
        case JoyDirection::LEFT: {
            auto ch = static_cast<uint8_t>(m_channel);
            if (ch == 0) ch = NUM_CHANNELS - 1;
            else ch--;
            m_channel = static_cast<Channel>(ch);
            m_count = 0;
            m_head = 0;
            m_yScaleMode = YScaleMode::DYNAMIC;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        }

        case JoyDirection::RIGHT: {
            auto ch = static_cast<uint8_t>(m_channel);
            ch = (ch + 1) % NUM_CHANNELS;
            m_channel = static_cast<Channel>(ch);
            m_count = 0;
            m_head = 0;
            m_yScaleMode = YScaleMode::DYNAMIC;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        }

        case JoyDirection::UP:
        case JoyDirection::DOWN:
            if (m_yScaleMode == YScaleMode::DYNAMIC) {
                m_yScaleMode = YScaleMode::FIXED;
                computeRange(m_fixedMin, m_fixedMax);
            } else {
                m_yScaleMode = YScaleMode::DYNAMIC;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            return InputResult::EXIT_SCREEN;

        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
