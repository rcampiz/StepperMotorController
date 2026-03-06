/**
 * @file terminal_screen.hpp
 * @brief Terminal screen for scrolling text console
 *
 * Displays a scrolling text buffer. Lines auto-scroll as new
 * content is added. Supports manual scrolling with joystick.
 */

#ifndef TERMINAL_SCREEN_HPP
#define TERMINAL_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

// Terminal configuration
constexpr uint8_t TERM_MAX_LINES = 20;       ///< Max lines in buffer
constexpr uint8_t TERM_LINE_LENGTH = 40;     ///< Max chars per line
constexpr uint8_t TERM_VISIBLE_LINES = 16;   ///< Lines visible at once

/**
 * @brief Terminal screen for scrolling text console
 *
 * Maintains a circular buffer of text lines. New text is appended
 * and the view auto-scrolls to show the latest content.
 *
 * Input:
 *   - UP/DOWN: Scroll view (disables auto-scroll)
 *   - CENTER: Jump to bottom, re-enable auto-scroll
 *   - LEFT: Exit terminal (returns EXIT_SCREEN)
 */
class TerminalScreen : public IScreen {
public:
    /**
     * @brief Construct a terminal screen
     * @param title Optional title (displayed at top, reduces visible lines)
     */
    explicit TerminalScreen(const char* title = nullptr);

    // IScreen interface
    ScreenType getType() const override { return ScreenType::TERMINAL; }
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

    /**
     * @brief Print a line of text
     * @param text Text to print (newline not required)
     *
     * Adds text to the buffer. If text is longer than TERM_LINE_LENGTH,
     * it will be truncated. Auto-scrolls to show new content.
     */
    void println(const char* text);

    /**
     * @brief Print formatted text (printf-style)
     * @param fmt Format string
     * @param ... Format arguments
     */
    void printf(const char* fmt, ...);

    /**
     * @brief Clear all text
     */
    void clear();

    /**
     * @brief Set terminal title
     * @param title Title string (copied, nullptr to remove title)
     */
    void setTitle(const char* title);

    /**
     * @brief Check if auto-scroll is enabled
     * @return true if auto-scrolling to bottom
     */
    bool isAutoScrollEnabled() const { return m_autoScroll; }

    /**
     * @brief Set auto-scroll state
     * @param enable true to auto-scroll to new content
     */
    void setAutoScroll(bool enable) { m_autoScroll = enable; }

    /**
     * @brief Get total line count
     * @return Number of lines in buffer
     */
    uint8_t getLineCount() const { return m_lineCount; }

private:
    char m_title[TERM_LINE_LENGTH];
    char m_lines[TERM_MAX_LINES][TERM_LINE_LENGTH];
    uint8_t m_lineCount;      // Total lines written (capped at TERM_MAX_LINES)
    uint8_t m_headIndex;      // Next write position in circular buffer
    uint8_t m_scrollOffset;   // View scroll offset from bottom
    bool m_autoScroll;        // Auto-scroll to new content
    bool m_hasTitle;          // Title is set
    bool m_needsRedraw;

    // Get line at logical index (0 = oldest visible)
    const char* getLine(uint8_t logicalIndex) const;

    // Calculate visible line count (accounts for title)
    uint8_t visibleLines() const;
};

} // namespace UI

#endif // TERMINAL_SCREEN_HPP
