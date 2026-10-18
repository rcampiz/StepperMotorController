/**
 * @file graph_screen.hpp
 * @brief Telemetry strip chart: one channel against time on a major/minor grid.
 *
 * One sample per display tick (50 ms), one pixel per sample, the newest at the right.
 * Rendering is a per-column diff: for every column only the span that changed between the
 * previous frame and this one is blitted (grid restored underneath), so a scrolling trace
 * costs a few pixels per column instead of the whole plot every 50 ms.
 */

#ifndef GRAPH_SCREEN_HPP
#define GRAPH_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class GraphScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Telemetry"; }
    const char* centerHint() const override { return "CHANNEL"; }

private:
    static constexpr uint8_t NUM_CHANNELS = 4;
    static constexpr uint16_t PLOT_X = 32;
    static constexpr uint16_t PLOT_Y = 62;
    static constexpr uint16_t PLOT_W = 200;      // 1 px per sample: 10 s at 20 Hz
    static constexpr uint16_t PLOT_H = 190;
    static constexpr uint16_t GRID_MINOR = 10;
    static constexpr uint16_t GRID_MAJOR = 50;
    static constexpr uint16_t SAMPLES = PLOT_W;

    enum class Channel : uint8_t { VELOCITY = 0, SPEED = 1, POSITION = 2, SLIP = 3 };
    enum class YScaleMode : uint8_t { DYNAMIC = 0, FIXED = 1 };

    Channel m_channel = Channel::VELOCITY;
    YScaleMode m_yScaleMode = YScaleMode::DYNAMIC;
    int32_t m_min = -10, m_max = 10;             // the range the plot is drawn with
    int32_t m_samples[SAMPLES] = {};
    uint16_t m_head = 0;
    uint16_t m_count = 0;
    bool m_needsRedraw = true;
    bool m_full = true;

    // What each column currently shows: the painted span [lo, hi] (rows), 0xFF = nothing
    uint8_t m_lo[PLOT_W] = {};
    uint8_t m_hi[PLOT_W] = {};
    uint8_t m_colBuf[PLOT_H * 2] = {};
    char m_valueText[24] = {};
    char m_rangeText[2][12] = {};

    void pushSample(int32_t value);
    int32_t getSample(uint16_t age) const;       // 0 = newest
    const char* channelName() const;
    const char* channelUnit() const;
    uint16_t channelColor() const;
    void dataRange(int32_t& outMin, int32_t& outMax) const;
    void updateRange();
    uint16_t rowOf(int32_t value) const;
    void drawFrame(Services::ICanvas& lcd);
    void drawAxisLabels(Services::ICanvas& lcd);
    void drawColumns(Services::ICanvas& lcd);
    void drawHeader(Services::ICanvas& lcd, bool force);
    void resetColumns();
};

} // namespace UI

#endif // GRAPH_SCREEN_HPP
