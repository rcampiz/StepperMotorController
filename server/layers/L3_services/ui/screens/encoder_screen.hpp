/**
 * @file encoder_screen.hpp
 * @brief Encoder screen: a dial with the shaft angle, the live counts, and a sweeping
 *        velocity scope.
 *
 * Everything static (ring, ticks, labels, scope frame) is drawn once on activation. Per
 * frame only what changed is sent: the needle (old one erased, new one drawn), the value
 * cells whose text changed, and one scope column.
 */

#ifndef ENCODER_SCREEN_HPP
#define ENCODER_SCREEN_HPP

#include "ui/screen.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <stdint.h>

namespace UI {

// Encoder counts per revolution (4x quadrature of a 1000-line disc; must match the GUI)
constexpr int32_t ENCODER_CPR = 4000;

class EncoderScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Encoder"; }
    const char* centerHint() const override { return nullptr; }

private:
    bool m_needsRedraw = true;
    bool m_full = true;

    // Dial
    static constexpr int16_t DIAL_CX = 120;
    static constexpr int16_t DIAL_CY = 116;
    static constexpr int16_t DIAL_R = 62;          // ring outer radius
    static constexpr int16_t NEEDLE_LEN = 44;      // inside the ticks
    uint16_t m_prevAngle1024 = 0xFFFF;             // 0xFFFF: no needle drawn yet
    bool m_prevIndexSeen = false;

    // Value cells, repainted when their text changes
    char m_cell[6][14] = {};

    // Velocity scope: sweeping cursor over SCOPE_W columns, sticky full-scale
    static constexpr uint16_t SCOPE_X = 8;
    static constexpr uint16_t SCOPE_Y = 250;
    static constexpr uint16_t SCOPE_W = 224;
    static constexpr uint16_t SCOPE_H = 44;
    uint16_t m_scopeCol = 0;
    int32_t m_scopeScale = 1000;                   // counts/s at full height, grows only
    uint8_t m_colBuf[SCOPE_H * 2] = {};

    void drawStatic(Services::ICanvas& lcd);
    void drawNeedle(Services::ICanvas& lcd, uint16_t angle1024, uint16_t color);
    void drawCell(Services::ICanvas& lcd, uint8_t i, const char* label, const char* value, uint16_t color);
    void drawScopeColumn(Services::ICanvas& lcd, int32_t velocity);
    static void i64toa(int64_t val, char* buf, int bufSize);
};

} // namespace UI

#endif // ENCODER_SCREEN_HPP
