/**
 * @file saver_screen.hpp
 * @brief Screen saver. Pushed by the DisplayService after a quiet period; the first
 *        joystick move pops it (and is swallowed). Three styles (ScreenCareSettings):
 *
 *  - SYNTHWAVE: a sky with twinkling stars, a striped sun whose bands slide down,
 *    wireframe mountains or a lit skyline, and a perspective grid that scrolls toward the
 *    viewer, and two VHS tracking bands rolling down over all of it (Graphics::VhsBands,
 *    the motion indicator's). Every 30 s a new scene: another palette (sunset, Blade Runner amber, cyber,
 *    acid), horizon height, sun size and place, so no pixel holds one picture. Once per
 *    scene "STEPPER / MOTOR / CONTROLLER" fades in on three lines, Hotline Miami style
 *    (tall slanted block letters, hard offset shadow, dark outline, neon colours that flash),
 *    with the release ("v0.5.0", Harness::FW_VERSION) under it in smaller letters of the same
 *    cut, rocks and bobs like that game's menu for 10 s and fades out. Per frame: the floor rows
 *    a grid line left or reached (a floor row is one colour or one line, its verticals fixed),
 *    the sun's box every other frame, the title's box while it shows, and the rows the bands
 *    left and reached. With the title, a pixel-art NEMA23 with its encoder hovers over the floor
 *    like a Hotline Miami pickup (a bob, an outline flashing neon colours, a shadow that breathes
 *    with its height; owner 2026-10-22), repainted on the frames the title leaves, its columns only. At the bottom, on a dark plate, what it is and the way out ("SCREEN
 *    SAVER" / "PRESS ANY BUTTON TO EXIT": someone walking up to the panel may not know).
 *  - HOTLINE: a diagonal stripe band and a blocky SMC card with chromatic aberration,
 *    relaid every 2 s, with VHS glitch slices in between.
 *  - BLANK: a black panel, nothing sent.
 */

#ifndef SAVER_SCREEN_HPP
#define SAVER_SCREEN_HPP

#include "ui/screen.hpp"
#include "ui/screen_care.hpp"
#include "graphics/vhs_bands/vhs_bands.hpp"
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
    void setNowMs(uint32_t nowMs) override { m_nowMs = nowMs; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

    // Synthwave scene geometry (public for the host tests)
    static constexpr uint16_t SCENE_FRAMES = 600;  ///< 30 s at 20 Hz, then a new scene
    static constexpr uint16_t TITLE_START_FRAME = 20;    ///< the title starts 1 s into a scene (was 5 s:
                                                         ///< the owner wanted it sooner, 2026-10-21)
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
    Graphics::VhsBands m_vhs;              // the tracking bands, on the clock
    uint32_t m_nowMs = 0;
    Services::ICanvas* m_canvas = nullptr; // for the exit hint's glyphs
    bool m_motorShown = false;             // the hovering motor as drawn: shown, its bob, its alpha
    int16_t m_motorBob = 0;
    uint16_t m_motorAlpha = 0;
    uint8_t m_motorInk = 0;                // the outline's flash colour as drawn
    void synthNewScene();
    void synthFullPaint(Services::ICanvas& lcd);
    bool floorLine(int16_t r, float phase) const;          // floor row r (1 = under the horizon) is a grid line
    void floorRow(int16_t y, int16_t xa, int16_t xb, uint8_t* buf) const;   // floor columns [xa, xb], buf[0] = xa
    void synthRows(Services::ICanvas& lcd, int16_t first, int16_t last, int16_t xa = 0, int16_t xb = 239);  // sky or floor
    void synthSunBox(Services::ICanvas& lcd);
    void synthTwinkle(Services::ICanvas& lcd);
    void synthTitle(Services::ICanvas& lcd, uint16_t t);
    void synthPrepareTitle(uint16_t t);    // this frame's rock, bob and colours
    /// Columns [xa, xb] of sky row y into buf (big-endian RGB565, buf[0] = column xa): the
    /// scene by runs, the stars, the title as last prepared, the bands
    void composeUpper(int16_t y, int16_t xa, int16_t xb, uint8_t* buf) const;
    /// The hovering motor and its shadow over columns [xa, xb] of row y (buf[0] = column xa)
    void overlayMotor(int16_t y, int16_t xa, int16_t xb, uint8_t* buf) const;
    void synthUpperRows(Services::ICanvas& lcd, int16_t first, int16_t last);   // sky rows, all of each
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
