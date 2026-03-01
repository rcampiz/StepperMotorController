/**
 * @file encoder_screen.hpp
 * @brief Encoder monitor screen with numerical values and circular dial
 *
 * Top half: numerical values (count, velocity, RPM, angle, revolutions)
 * Bottom half: circular dial with tick marks and red position dot
 *
 * Uses composited scanline rendering for the dial (same technique as
 * indicator_service.cpp) for flicker-free updates.
 *
 * No RTOS headers. No driver dependencies beyond LCD.
 */

#ifndef ENCODER_SCREEN_HPP
#define ENCODER_SCREEN_HPP

#include "ui/screen.hpp"
#include "F_platform/interfaces/telemetry.hpp"
#include <stdint.h>

class LCD;

namespace UI {

// Configurable encoder counts per revolution (4x quadrature)
// 1000-line encoder at 4x = 4000 CPR (must match GUI ENCODER_CPR)
constexpr int32_t ENCODER_CPR = 4000;

class EncoderScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    bool m_needsRedraw = true;

    // Previous dot position for dirty-rect erase
    int16_t m_prevDotX = -1;
    int16_t m_prevDotY = -1;
    int16_t m_prevAngleDeg = -1;
    bool m_dialDrawn = false;
    bool m_labelsDrawn = false;

    // Cached previous values for dirty-flag rendering
    int64_t m_prevCount = 0;
    int32_t m_prevVelocity = 0;
    int32_t m_prevRevX100 = 0;   // count/CPR scaled by 100
    int32_t m_prevRsX100 = 0;    // rev/s scaled by 100
    int32_t m_prevRpmX10 = 0;    // RPM scaled by 10
    uint32_t m_prevIndexPeriod = 0;

    // Dial geometry
    static constexpr int16_t DIAL_CX = 120;
    static constexpr int16_t DIAL_CY = 220;
    static constexpr int16_t DIAL_RADIUS = 60;
    static constexpr int16_t TICK_OUTER = 65;    // Major tick outer extent
    static constexpr int16_t TICK_INNER = 53;    // Major tick inner extent
    static constexpr int16_t TICK_MINOR_OUTER = 62;
    static constexpr int16_t TICK_MINOR_INNER = 56;
    static constexpr int16_t DOT_RADIUS = 4;
    static constexpr int16_t DOT_ORBIT = 50;     // Distance from center for position dot

    // Numerical section height
    static constexpr uint16_t NUM_SECTION_Y = 160;

    void renderNumerical(LCD& lcd, const Comms::TelemetrySnapshot& telem);
    void renderDialStatic(LCD& lcd);
    void renderDialDot(LCD& lcd, int16_t angleDeg);

    // i64toa helper (newlib-nano doesn't support %lld)
    static void i64toa(int64_t val, char* buf, int bufSize);
};

} // namespace UI

#endif // ENCODER_SCREEN_HPP
