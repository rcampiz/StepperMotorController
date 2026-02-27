/**
 * @file trace_screen.hpp
 * @brief Trace monitor screen — live view of interface boundary events
 *
 * Reads from the Trace ring buffer (services/trace.hpp) and displays
 * entries in a scrollable list. Per CODE_PHILOSOPHY.md §9: observability
 * at boundaries without polluting logic.
 *
 * Input:
 *   - UP/DOWN: Scroll (disables auto-scroll)
 *   - CENTER: Jump to newest, re-enable auto-scroll
 *   - LEFT: Exit
 */

#ifndef TRACE_SCREEN_HPP
#define TRACE_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

class LCD;

namespace UI {

class TraceScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::TERMINAL; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    size_t m_lastCount = 0;       // Last seen trace entry count
    size_t m_scrollOffset = 0;    // Offset from bottom (0 = newest visible)
    bool m_autoScroll = true;

    static constexpr uint8_t VISIBLE_LINES = 24;
    static constexpr uint16_t LINE_HEIGHT = 12;
};

} // namespace UI

#endif // TRACE_SCREEN_HPP
