/**
 * @file estop_screen.hpp
 * @brief The E-STOP / FAULT page: pushed by the DisplayService whenever the stop latch or a
 *        driver fault is set (from any source), popped when it clears. Shows what is latched
 *        and the way out: hold CENTER for 3 s (a bar fills; letting go cancels). The page is
 *        modal: no other input acts and the 2 s "home" hold is off while it shows.
 *
 *        A stop can stay latched for hours, so nothing on the page stays in one place: the
 *        hazard stripes crawl and the red grid scrolls every frame, and the page runs a cycle:
 *        it shows for SHOW_FRAMES, fades out, leaves only the stripes and grid for DARK_FRAMES,
 *        then fades in on the next of eight layouts. The layouts swap the grid between floor
 *        and ceiling (the middle moves ~50 px up or down with it), shift it sideways and change
 *        its colours, so no pixel holds one picture. Holding CENTER, or a new message, brings
 *        the page back at once and holds it while the bar fills. The title is the saver's
 *        Hotline Miami lettering (slanted, extruded shadow, flashing hot colours), rocking and
 *        bobbing all the while it shows.
 *
 *        Two VHS tracking bands (Graphics::VhsBands, the motion indicator's) roll down the
 *        page: over the hazard stripes, the grid, the page and behind the title's letters,
 *        never over the status panel, the prompt, the bar or a message (they stay
 *        readable). Each frame the rows the bands left and reached are recomposed.
 */

#ifndef ESTOP_SCREEN_HPP
#define ESTOP_SCREEN_HPP

#include "ui/screen.hpp"
#include "graphics/vhs_bands/vhs_bands.hpp"
#include <stdint.h>

namespace UI {

class EstopScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override { (void)dir; (void)pressed; return InputResult::HANDLED; }
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

    void setFault(bool fault) { if (fault != m_fault) { m_fault = fault; m_full = true; } }
    void setProgress(uint8_t pct) { m_progress = pct > 100 ? 100 : pct; }
    void setMessage(const char* msg);

    /// The bar's home position (layout 0; other layouts offset it like the rest of the middle)
    static constexpr uint16_t BAR_X = 20, BAR_Y = 200, BAR_W = 200, BAR_H = 12;
    static constexpr uint16_t SHOW_FRAMES = 300;     ///< 15 s at 20 Hz in one place
    static constexpr uint16_t FADE_FRAMES = 10;      ///< 0.5 s fade out, and in
    static constexpr uint16_t DARK_FRAMES = 14;      ///< 0.7 s of stripes and grid only
    static constexpr uint16_t SHIFT_FRAMES = SHOW_FRAMES + 2 * FADE_FRAMES + DARK_FRAMES;   ///< one cycle

    /// Where the middle content sits now (tests): offset from its home position
    int16_t offsetX() const { return m_offX; }
    int16_t offsetY() const { return m_offY; }
    bool gridOnTop() const { return m_gridTop; }

private:
    enum class Phase : uint8_t { SHOW, FADE_OUT, DARK, FADE_IN };

    bool m_needsRedraw = true;
    bool m_full = true;
    bool m_fault = false;
    uint8_t m_progress = 0;
    uint8_t m_drawnProgress = 0xFF;
    char m_message[48] = {};
    char m_drawnMessage[48] = {};
    bool m_messageNew = false;
    uint16_t m_drawnStatus = 0xFFFF;   ///< faults | readable<<8 as last drawn

    // Motion against burn-in
    Phase m_phase = Phase::SHOW;
    uint16_t m_phaseFrames = 0;
    uint16_t m_alpha = 256;          ///< the middle's strength, 0..256
    uint8_t m_layout = 0;
    uint8_t m_pal = 0;
    int16_t m_offX = 0, m_offY = 0;
    bool m_gridTop = false;
    uint8_t m_stripePhase = 0;
    uint16_t m_tick = 0;             ///< frames since the page came up: the title's rock and flash
    float m_gridPhase = 0.0f;
    Graphics::VhsBands m_vhs;        ///< the tracking bands, on the frame clock
    uint32_t m_vhsTicks = 0;

    void applyLayout();
    uint16_t ink(uint16_t c) const;  ///< c faded toward the page by m_alpha
    void drawStatic(Services::ICanvas& lcd);
    void drawTitle(Services::ICanvas& lcd);
    void drawMiddle(Services::ICanvas& lcd);
    void drawFaults(Services::ICanvas& lcd, uint8_t faults, bool readable);
    void drawBands(Services::ICanvas& lcd);
    void drawGrid(Services::ICanvas& lcd);
    void gridRow(uint16_t i, uint8_t* row) const;          ///< the grid's i-th row, a panel row
    void stripeRow(uint16_t r, uint8_t* row) const;        ///< a hazard band's r-th row
    void prepareTitle();                                  ///< this frame's rock, bob and colours
    bool titlePixel(int16_t x, int16_t y, uint16_t& c) const;   ///< a letter's pixel, as prepared
    bool keepOut(int16_t y, int16_t& a, int16_t& b) const;      ///< columns the bands leave alone
    void composeRow(int16_t y, uint8_t* row) const;       ///< a whole panel row, bands included
    void paintBandRows(Services::ICanvas& lcd, int16_t first, int16_t last);
};

} // namespace UI

#endif // ESTOP_SCREEN_HPP
