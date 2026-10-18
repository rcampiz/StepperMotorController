/**
 * @file estop_screen.hpp
 * @brief The E-STOP / FAULT page: pushed by the DisplayService whenever the stop latch or a
 *        driver fault is set (from any source), popped when it clears. Shows what is latched
 *        and the way out: hold CENTER for 3 s (a bar fills; letting go cancels). The page is
 *        modal: no other input acts and the 2 s "home" hold is off while it shows.
 *
 *        A stop can stay latched for hours, so nothing on the page stands still: the hazard
 *        stripes crawl, the red grid scrolls toward the viewer, and every SHIFT_FRAMES the
 *        middle (title, panel, prompt, bar) moves to the next of eight small offsets.
 */

#ifndef ESTOP_SCREEN_HPP
#define ESTOP_SCREEN_HPP

#include "ui/screen.hpp"
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

    static constexpr uint16_t BAR_X = 20, BAR_Y = 200, BAR_W = 200, BAR_H = 12;
    static constexpr uint16_t SHIFT_FRAMES = 400;    ///< 20 s at 20 Hz between layout shifts

    /// Where the middle content sits now (tests): offset from its home position
    int16_t offsetX() const { return m_offX; }
    int16_t offsetY() const { return m_offY; }

private:
    bool m_needsRedraw = true;
    bool m_full = true;
    bool m_fault = false;
    uint8_t m_progress = 0;
    uint8_t m_drawnProgress = 0xFF;
    char m_message[48] = {};
    char m_drawnMessage[48] = {};
    uint16_t m_drawnStatus = 0xFFFF;
    bool m_drawnStall = false;

    // Motion against burn-in
    uint16_t m_frames = 0;
    uint8_t m_shift = 0;
    int16_t m_offX = 0, m_offY = 0;
    uint8_t m_stripePhase = 0;
    float m_gridPhase = 0.0f;

    void drawStatic(Services::ICanvas& lcd);
    void drawFaults(Services::ICanvas& lcd, uint16_t statusReg, bool stalled);
    void drawBands(Services::ICanvas& lcd);
    void drawGrid(Services::ICanvas& lcd);
};

} // namespace UI

#endif // ESTOP_SCREEN_HPP
