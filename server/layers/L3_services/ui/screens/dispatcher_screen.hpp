/**
 * @file dispatcher_screen.hpp
 * @brief Command dispatcher activity screen
 *
 * Shows command dispatch statistics, queue depths, event stats, per-service activity bars
 * from the trace ring, the command- and event-queue depth history and a scrollable list of
 * the recent commands. Drawn under the shared chrome; only what changed is repainted.
 *
 * Controls:
 *   UP/DOWN: Scroll recent commands
 *   LEFT: Exit screen
 */

#ifndef DISPATCHER_SCREEN_HPP
#define DISPATCHER_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class DispatcherScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Dispatcher"; }
    const char* centerHint() const override { return nullptr; }

private:
    static constexpr uint8_t SVC_COUNT = 8;
    static constexpr uint8_t RECENT_VISIBLE = 4;
    static constexpr uint8_t RECENT_MAX = 8;
    static constexpr uint8_t CELL_COUNT = 6;

    bool m_needsRedraw = true;      ///< the manager clears and frames the page
    bool m_full = true;             ///< static parts still to draw
    uint8_t m_scrollOffset = 0;

    // Queue depth history (one sample per frame, newest on the right)
    static constexpr uint8_t DEPTH_HISTORY_SIZE = 60;
    uint8_t m_cmdQueueHistory[DEPTH_HISTORY_SIZE] = {};
    uint8_t m_evtQueueHistory[DEPTH_HISTORY_SIZE] = {};
    uint8_t m_historyHead = 0;
    uint8_t m_historyCount = 0;

    // What is on the panel now, so a frame repaints only what changed
    char m_cell[CELL_COUNT][24] = {};
    uint16_t m_cellColor[CELL_COUNT] = {};
    uint16_t m_svcFill[SVC_COUNT] = {};     ///< 0xFFFF: not drawn yet
    uint8_t m_svcPct[SVC_COUNT] = {};
    char m_svcHead[24] = {};
    uint8_t m_drawnCmd[DEPTH_HISTORY_SIZE] = {};   ///< 0xFF: not drawn yet
    uint8_t m_drawnEvt[DEPTH_HISTORY_SIZE] = {};
    char m_recent[RECENT_VISIBLE][40] = {};
    char m_scrollText[40] = {};

    void drawStatic(Services::ICanvas& lcd);
    void drawCell(Services::ICanvas& lcd, uint8_t i, const char* value, uint16_t color);
    void drawServiceBars(Services::ICanvas& lcd);
    void drawHistory(Services::ICanvas& lcd, uint16_t y, const uint8_t* history, uint8_t* drawn);
    void drawRecent(Services::ICanvas& lcd, const char (*cmds)[24], uint8_t count);
};

} // namespace UI

#endif // DISPATCHER_SCREEN_HPP
