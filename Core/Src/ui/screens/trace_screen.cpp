/**
 * @file trace_screen.cpp
 * @brief Trace monitor screen implementation
 */

#include "ui/screens/trace_screen.hpp"
#include "drivers/lcd_st7789.hpp"
#include "services/trace.hpp"
#include <stdio.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LABEL_COLOR = 0x07FF;  // Cyan
static constexpr uint16_t TEXT_COLOR = 0x07E0;    // Green
static constexpr uint16_t BG_COLOR = 0x0000;

void TraceScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_scrollOffset = 0;
    m_autoScroll = true;
    m_lastCount = 0;
}

void TraceScreen::render(LCD& lcd)
{
    size_t count = Trace::getCount();

    // Draw title once
    if (!m_titleDrawn) {
        lcd.drawString(MARGIN, MARGIN, "Trace Monitor", LABEL_COLOR, BG_COLOR);
        lcd.drawHLine(MARGIN, MARGIN + TITLE_H, LCD::WIDTH - 2 * MARGIN, LABEL_COLOR);
        m_titleDrawn = true;
    }

    // Auto-scroll when new entries arrive
    if (count != m_lastCount) {
        if (m_autoScroll) {
            m_scrollOffset = 0;
        }
        m_lastCount = count;
    } else {
        // Nothing changed and not first render — skip
        if (m_titleDrawn && !m_needsRedraw) return;
    }

    uint16_t y = MARGIN + TITLE_H + 4;

    // Determine which entries to show
    size_t startLine = 0;
    if (count > VISIBLE_LINES) {
        if (count > m_scrollOffset + VISIBLE_LINES) {
            startLine = count - VISIBLE_LINES - m_scrollOffset;
        }
    }

    Trace::Entry entry;
    char lineBuf[42];

    for (uint8_t i = 0; i < VISIBLE_LINES; i++) {
        size_t idx = startLine + i;
        lcd.fillRect(MARGIN, y, LCD::WIDTH - 2 * MARGIN, LINE_HEIGHT, BG_COLOR);

        if (idx < count && Trace::getEntry(idx, entry)) {
            char dirCh = (entry.dir == Trace::ENTRY) ? '>' : '<';
            if (entry.arg0 != 0) {
                snprintf(lineBuf, sizeof(lineBuf), "%c %s %lu",
                         dirCh, entry.tag,
                         static_cast<unsigned long>(entry.arg0));
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "%c %s", dirCh, entry.tag);
            }
            lcd.drawString(MARGIN, y, lineBuf, TEXT_COLOR, BG_COLOR);
        }

        y += LINE_HEIGHT;
    }
}

InputResult TraceScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    size_t count = Trace::getCount();
    size_t maxScroll = (count > VISIBLE_LINES) ? (count - VISIBLE_LINES) : 0;

    switch (dir) {
        case JoyDirection::UP:
            if (m_scrollOffset < maxScroll) {
                m_scrollOffset++;
                m_autoScroll = false;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
                m_needsRedraw = true;
                if (m_scrollOffset == 0) {
                    m_autoScroll = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            m_scrollOffset = 0;
            m_autoScroll = true;
            m_needsRedraw = true;
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
