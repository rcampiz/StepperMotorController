/**
 * @file saver_screen.hpp
 * @brief Screen saver. Pushed by the DisplayService after a quiet period; the first
 *        joystick move pops it (and is swallowed). Three styles (ScreenCareSettings):
 *
 *  - SYNTHWAVE: a sky with twinkling stars, a striped sun whose bands slide down,
 *    wireframe mountains or a lit skyline, and a perspective grid that scrolls toward the
 *    viewer. Every 30 s a new scene: another palette (sunset, Blade Runner amber, cyber,
 *    acid), horizon height, sun size and place, so no pixel holds one picture. Once per
 *    scene "STEPPER / MOTOR / CONTROLLER" fades in on three lines, Hotline Miami style
 *    (tall slanted block letters, hard offset shadow, dark outline, neon colours that flash),
 *    rocks and bobs like that game's menu for 10 s and fades out. Per frame the floor and the
 *    sun's box are repainted, and the title's box every other frame while it shows.
 *  - HOTLINE: a diagonal stripe band and a blocky SMC card with chromatic aberration,
 *    relaid every 2 s, with VHS glitch slices in between.
 *  - BLANK: a black panel, nothing sent.
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

    // Synthwave scene geometry (public for the host tests)
    static constexpr uint16_t SCENE_FRAMES = 600;  ///< 30 s at 20 Hz, then a new scene
    static constexpr uint16_t TITLE_START_FRAME = 100;   ///< the title starts 5 s into a scene
    int16_t horizon() const { return m_horizon; }
    uint8_t palette() const { return m_palette; }

private:
    const ScreenCareSettings* m_settings;
    bool m_needsRedraw = true;
    uint32_t m_seed = 0x2545F491u;
    uint32_t nextRandom();

    // --- synthwave -------------------------------------------------------------------------
    static constexpr uint8_t STAR_COUNT = 48;
    bool m_synthReady = false;
    uint16_t m_frame = 0;
    float m_phase = 0.0f;                  // grid scroll, line spacings
    uint8_t m_stripe = 0;                  // sun stripe scroll
    int16_t m_sunX = 120;
    int16_t m_sunR = 46;
    int16_t m_horizon = 200;
    uint8_t m_palette = 0;
    bool m_city = false;
    uint32_t m_seedScene = 0;
    int16_t m_titleX = 31;                 // the title block's top-left this scene
    int16_t m_titleY = 12;
    uint16_t m_titleAlpha = 0;             // 0 = not shown, 256 = full
    uint8_t m_ridge[240] = {};             // mountain / building height above the horizon per column
    uint8_t m_bldStart[240] = {};          // skyline: the column each building starts at
    uint16_t m_sky[240] = {};              // sky colour per row (horizon < 240)
    uint8_t m_starX[STAR_COUNT] = {};
    uint8_t m_starY[STAR_COUNT] = {};
    void synthNewScene();
    void synthFullPaint(Services::ICanvas& lcd);
    void synthFloor(Services::ICanvas& lcd);
    void synthSunBox(Services::ICanvas& lcd);
    void synthTwinkle(Services::ICanvas& lcd);
    void synthTitle(Services::ICanvas& lcd, uint16_t t);
    uint16_t synthUpper(int16_t x, int16_t y) const;   // sky, sun, mountains, horizon line
    void renderSynthwave(Services::ICanvas& lcd);

    // --- hotline ---------------------------------------------------------------------------
    static constexpr uint16_t HOLD_FRAMES = 40;     // 2 s at 20 Hz between layouts
    static constexpr uint16_t GLITCH_FRAMES = 5;    // a glitch slice every 250 ms
    static constexpr uint16_t CELL = 6;             // title glyph cell (5x7 glyphs -> 30x42)
    static constexpr uint16_t CARD_W = 3 * 6 * CELL + 12;
    static constexpr uint16_t CARD_H = 7 * CELL + 6;
    static constexpr uint16_t BAND_H = 56;
    uint16_t m_frames = 0;
    uint16_t m_glitchFrames = 0;
    bool m_drawn = false;
    uint16_t m_cardX = 0, m_cardY = 0;
    uint16_t m_bandY = 0;
    uint8_t m_bandPhase = 0;
    uint16_t m_glitchY = 0, m_glitchX = 0, m_glitchW = 0;
    bool m_glitchDrawn = false;
    void drawBand(Services::ICanvas& lcd, uint16_t y, uint8_t phase, bool erase);
    void drawCard(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase);
    void drawGlyph(Services::ICanvas& lcd, const uint8_t* glyph, uint16_t x, uint16_t y, uint16_t color);
    void layout(Services::ICanvas& lcd);
    void glitch(Services::ICanvas& lcd);
    void renderHotline(Services::ICanvas& lcd);
};

} // namespace UI

#endif // SAVER_SCREEN_HPP
