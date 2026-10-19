/**
 * @file memory_screen.hpp
 * @brief Memory: how full the flash and the RAM are, where the RAM goes, and how deep every
 *        stack has ever been, at a glance.
 *
 * Two VU-meter gauges (flash, static RAM): segments green below 70 %, yellow to 90 %, red
 * beyond, the lit ones bright, with the percentage in large type and used / free under them.
 * A split bar shows the RAM as task stacks, other globals and free. The stack table has the
 * main stack (boot and interrupts) and each task: its peak use against its size, the same
 * colours. Read-only; LEFT goes back. Sampled once a second, repainted only where a figure
 * changed. Data: Platform::IMemoryStats and Platform::ITaskStats (the power net).
 */

#ifndef MEMORY_SCREEN_HPP
#define MEMORY_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class MemoryScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Memory"; }
    const char* centerHint() const override { return nullptr; }

    static constexpr uint8_t SEGMENTS = 25;      ///< gauge segments, 4 % each
    static constexpr uint8_t MAX_ROWS = 6;       ///< stack table: main stack + 5 tasks
    static constexpr uint16_t FRAMES_PER_SAMPLE = 20;   ///< Display task at 20 Hz: 1 Hz

private:
    struct Gauge {                    ///< what one gauge shows on the panel
        uint8_t lit;                  ///< 0xFF: nothing drawn yet
        char total[20];
        char pct[8];
        char used[20];
        char free[20];
    };
    struct Row {                      ///< one stack table row on the panel
        bool valid;
        char name[8];
        uint8_t lit;
        uint16_t color;
        char pct[8];
        char detail[20];
    };

    void drawStatic(Services::ICanvas& lcd);
    void drawGauge(Services::ICanvas& lcd, uint8_t g, uint32_t used, uint32_t total);
    void drawSplit(Services::ICanvas& lcd, uint32_t stacks, uint32_t globals, uint32_t total);
    void drawRows(Services::ICanvas& lcd);

    bool m_needsRedraw = true;    ///< the manager clears and frames the page
    bool m_full = true;           ///< static parts to draw
    bool m_haveData = false;
    uint16_t m_frame = 0;
    Gauge m_gauge[2] = {};
    uint16_t m_splitA = 0xFFFF, m_splitB = 0xFFFF;    ///< split bar boundaries on the panel (px)
    char m_legend[3][16] = {};
    Row m_rows[MAX_ROWS] = {};
};

} // namespace UI

#endif // MEMORY_SCREEN_HPP
