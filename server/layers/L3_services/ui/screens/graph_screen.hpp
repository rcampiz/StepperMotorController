/**
 * @file graph_screen.hpp
 * @brief Rolling telemetry graph screen
 *
 * Displays a scrolling bar graph of telemetry data. Three channels
 * selectable with LEFT/RIGHT: encoder velocity, motor speed, position.
 * Auto-scales Y axis to min/max of visible data.
 *
 * No RTOS headers.
 */

#ifndef GRAPH_SCREEN_HPP
#define GRAPH_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

class LCD;

namespace UI {

class GraphScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    static constexpr uint8_t NUM_CHANNELS = 4;
    static constexpr uint16_t GRAPH_SAMPLES = 188;
    static constexpr uint16_t GRAPH_X = 50;      // Left margin for Y-axis labels (6ch × 8px + 2px)
    static constexpr uint16_t GRAPH_Y = 18;       // Below header
    static constexpr uint16_t GRAPH_W = 188;      // 1 pixel per sample
    static constexpr uint16_t GRAPH_H = 280;      // Tall graph area

    enum class Channel : uint8_t {
        VELOCITY = 0,  // Encoder velocity (counts/s)
        SPEED = 1,     // Motor speed (steps/s)
        POSITION = 2,  // Motor position (steps)
        SLIP = 3       // Following error (encoder ticks)
    };

    enum class YScaleMode : uint8_t {
        DYNAMIC = 0,   // Auto-scale to visible data (always includes 0)
        FIXED = 1      // Locked range for visual comparison
    };

    Channel m_channel = Channel::VELOCITY;
    YScaleMode m_yScaleMode = YScaleMode::DYNAMIC;
    int32_t m_fixedMin = 0;
    int32_t m_fixedMax = 1;
    int32_t m_samples[GRAPH_SAMPLES] = {};
    uint8_t m_colBuf[GRAPH_H * 2] = {};  // Column pixel buffer for flicker-free rendering
    uint16_t m_head = 0;       // Next write position (ring buffer)
    uint16_t m_count = 0;      // Samples written (up to GRAPH_SAMPLES)
    bool m_needsRedraw = true;

    void pushSample(int32_t value);
    int32_t getSample(uint16_t age) const;  // 0 = newest
    const char* channelName() const;
    uint16_t channelColor() const;
    const char* channelUnit() const;
    void computeRange(int32_t& outMin, int32_t& outMax) const;
    void renderGraph(LCD& lcd);
    void renderHeader(LCD& lcd);
};

} // namespace UI

#endif // GRAPH_SCREEN_HPP
