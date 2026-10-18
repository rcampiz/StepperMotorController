/**
 * @file saver_screen.hpp
 * @brief Screen saver in the Hotline Miami register: a diagonal hot-pink / cyan / violet
 *        stripe band and a blocky "SMC" title card with chromatic-aberration offsets, both
 *        relaid every two seconds somewhere else on the panel, with VHS glitch slices in
 *        between. Or a blank panel. Pushed by the DisplayService after a quiet period; the
 *        first joystick move pops it (and is swallowed).
 */

#ifndef SAVER_SCREEN_HPP
#define SAVER_SCREEN_HPP

#include "ui/screen.hpp"
#include "ui/screen_care.hpp"
#include <stdint.h>

namespace UI {

class SaverScreen : public IScreen {
public:
    explicit SaverScreen(const ScreenCareSettings* settings) : m_settings(settings) {}
    ScreenType getType() const override { return ScreenType::IMAGE; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    static constexpr uint16_t HOLD_FRAMES = 40;     // 2 s at 20 Hz between layouts
    static constexpr uint16_t GLITCH_FRAMES = 5;    // a glitch slice every 250 ms
    static constexpr uint16_t CELL = 6;             // title glyph cell (5x7 glyphs -> 30x42)
    static constexpr uint16_t CARD_W = 3 * 6 * CELL + 12;   // three glyphs, gaps, aberration
    static constexpr uint16_t CARD_H = 7 * CELL + 6;
    static constexpr uint16_t BAND_H = 56;

    const ScreenCareSettings* m_settings;
    bool m_needsRedraw = true;
    uint16_t m_frames = 0;
    uint16_t m_glitchFrames = 0;
    bool m_drawn = false;
    uint16_t m_cardX = 0, m_cardY = 0;
    uint16_t m_bandY = 0;
    uint8_t m_bandPhase = 0;
    uint16_t m_glitchY = 0, m_glitchX = 0, m_glitchW = 0;
    bool m_glitchDrawn = false;
    uint32_t m_seed = 0x2545F491u;

    uint32_t nextRandom();
    void drawBand(Services::ICanvas& lcd, uint16_t y, uint8_t phase, bool erase);
    void drawCard(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase);
    void drawGlyph(Services::ICanvas& lcd, const uint8_t* glyph, uint16_t x, uint16_t y, uint16_t color);
    void layout(Services::ICanvas& lcd);
    void glitch(Services::ICanvas& lcd);
};

} // namespace UI

#endif // SAVER_SCREEN_HPP
