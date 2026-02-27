/**
 * @file terminal_screen.cpp
 * @brief Terminal screen implementation
 */

#include "ui/terminal_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

namespace UI {

// Layout constants
static constexpr uint16_t MARGIN_LEFT = 4;
static constexpr uint16_t MARGIN_TOP = 4;
static constexpr uint16_t TITLE_HEIGHT = 16;
static constexpr uint16_t LINE_HEIGHT = 12;

// Colors
static constexpr uint16_t COLOR_TITLE = 0x07FF;    // Cyan
static constexpr uint16_t COLOR_TEXT = 0x07E0;     // Green (terminal style)
static constexpr uint16_t COLOR_BG = 0x0000;       // Black
static constexpr uint16_t COLOR_SCROLL_IND = 0xF800; // Red (when not at bottom)

TerminalScreen::TerminalScreen(const char* title)
    : m_lineCount(0)
    , m_headIndex(0)
    , m_scrollOffset(0)
    , m_autoScroll(true)
    , m_hasTitle(false)
    , m_needsRedraw(true)
{
    memset(m_title, 0, sizeof(m_title));
    memset(m_lines, 0, sizeof(m_lines));

    if (title != nullptr) {
        setTitle(title);
    }
}

void TerminalScreen::setTitle(const char* title)
{
    if (title != nullptr) {
        strncpy(m_title, title, TERM_LINE_LENGTH - 1);
        m_title[TERM_LINE_LENGTH - 1] = '\0';
        m_hasTitle = true;
    } else {
        m_title[0] = '\0';
        m_hasTitle = false;
    }
    m_needsRedraw = true;
}

void TerminalScreen::println(const char* text)
{
    if (text == nullptr) {
        text = "";
    }

    // Copy text to current head position
    strncpy(m_lines[m_headIndex], text, TERM_LINE_LENGTH - 1);
    m_lines[m_headIndex][TERM_LINE_LENGTH - 1] = '\0';

    // Advance head (circular buffer)
    m_headIndex = (m_headIndex + 1) % TERM_MAX_LINES;

    // Update line count
    if (m_lineCount < TERM_MAX_LINES) {
        m_lineCount++;
    }

    // Auto-scroll to show new content
    if (m_autoScroll) {
        m_scrollOffset = 0;
    }

    m_needsRedraw = true;
}

void TerminalScreen::printf(const char* fmt, ...)
{
    char buf[TERM_LINE_LENGTH];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    println(buf);
}

void TerminalScreen::clear()
{
    m_lineCount = 0;
    m_headIndex = 0;
    m_scrollOffset = 0;
    m_autoScroll = true;
    memset(m_lines, 0, sizeof(m_lines));
    m_needsRedraw = true;
}

void TerminalScreen::onActivate()
{
    m_needsRedraw = true;
}

uint8_t TerminalScreen::visibleLines() const
{
    uint8_t lines = TERM_VISIBLE_LINES;
    if (m_hasTitle) {
        lines -= 2; // Title + separator takes space
    }
    return lines;
}

const char* TerminalScreen::getLine(uint8_t logicalIndex) const
{
    if (logicalIndex >= m_lineCount) {
        return "";
    }

    // Calculate actual index in circular buffer
    // logicalIndex 0 = oldest line
    // logicalIndex (m_lineCount-1) = newest line (at m_headIndex-1)

    // Offset from oldest line
    uint8_t oldest;
    if (m_lineCount < TERM_MAX_LINES) {
        oldest = 0;
    } else {
        oldest = m_headIndex; // Oldest is at head (about to be overwritten)
    }

    uint8_t actualIndex = (oldest + logicalIndex) % TERM_MAX_LINES;
    return m_lines[actualIndex];
}

void TerminalScreen::render(LCD& lcd)
{
    uint16_t y = MARGIN_TOP;

    // Draw title if set
    if (m_hasTitle) {
        lcd.drawString(MARGIN_LEFT, y, m_title, COLOR_TITLE, COLOR_BG);
        y += TITLE_HEIGHT;

        // Draw separator
        lcd.drawHLine(MARGIN_LEFT, y, LCD::WIDTH - 2 * MARGIN_LEFT, COLOR_TITLE);
        y += 4;
    }

    // Calculate which lines to show
    uint8_t visCount = visibleLines();
    uint8_t startLine = 0;

    if (m_lineCount > visCount) {
        // We have more lines than can be displayed
        // startLine is offset from the oldest viewable line
        // m_scrollOffset is offset from bottom (0 = showing newest)
        if (m_lineCount > m_scrollOffset + visCount) {
            startLine = m_lineCount - visCount - m_scrollOffset;
        }
    }

    // Draw visible lines
    for (uint8_t i = 0; i < visCount; i++) {
        uint8_t lineIndex = startLine + i;
        const char* text = "";

        if (lineIndex < m_lineCount) {
            text = getLine(lineIndex);
        }

        // Clear line area and draw text
        lcd.fillRect(MARGIN_LEFT, y, LCD::WIDTH - 2 * MARGIN_LEFT, LINE_HEIGHT, COLOR_BG);
        lcd.drawString(MARGIN_LEFT, y, text, COLOR_TEXT, COLOR_BG);
        y += LINE_HEIGHT;
    }

    // Draw scroll indicator if not at bottom
    if (m_scrollOffset > 0) {
        lcd.fillRect(LCD::WIDTH - 8, LCD::HEIGHT - 8, 4, 4, COLOR_SCROLL_IND);
    }
}

InputResult TerminalScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    uint8_t visCount = visibleLines();
    uint8_t maxScroll = (m_lineCount > visCount) ? (m_lineCount - visCount) : 0;

    switch (dir) {
        case JoyDirection::UP:
            // Scroll up (view older lines)
            if (m_scrollOffset < maxScroll) {
                m_scrollOffset++;
                m_autoScroll = false;
                m_needsRedraw = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            // Scroll down (view newer lines)
            if (m_scrollOffset > 0) {
                m_scrollOffset--;
                m_needsRedraw = true;
                if (m_scrollOffset == 0) {
                    m_autoScroll = true;
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            // Jump to bottom, enable auto-scroll
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
