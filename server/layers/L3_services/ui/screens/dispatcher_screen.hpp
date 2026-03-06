/**
 * @file dispatcher_screen.hpp
 * @brief Command dispatcher activity screen
 *
 * Shows command dispatch statistics, queue depths, event stats,
 * and a scrollable recent-commands history with a graphical
 * queue depth bar.
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
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return false; }
    void clearRedrawFlag() override { }

private:
    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    uint8_t m_scrollOffset = 0;

    // Queue depth history for graphical bar
    static constexpr uint8_t DEPTH_HISTORY_SIZE = 60;
    uint8_t m_cmdQueueHistory[DEPTH_HISTORY_SIZE] = {};
    uint8_t m_evtQueueHistory[DEPTH_HISTORY_SIZE] = {};
    uint8_t m_historyHead = 0;
    uint8_t m_historyCount = 0;

    void renderQueueBar(Harness::ICanvas& lcd, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        const uint8_t* history, uint8_t maxDepth);
};

} // namespace UI

#endif // DISPATCHER_SCREEN_HPP
