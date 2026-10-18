/**
 * @file estop_screen.hpp
 * @brief The E-STOP / FAULT page: pushed by the DisplayService whenever the stop latch or a
 *        driver fault is set (from any source), popped when it clears. Shows what is latched
 *        and the way out: hold CENTER for 3 s (a bar fills; letting go cancels). The page is
 *        modal: no other input acts and the 2 s "home" hold is off while it shows.
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

    static constexpr uint16_t BAR_X = 20, BAR_Y = 236, BAR_W = 200, BAR_H = 14;

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

    void drawStatic(Services::ICanvas& lcd);
    void drawFaults(Services::ICanvas& lcd, uint16_t statusReg, bool stalled);
};

} // namespace UI

#endif // ESTOP_SCREEN_HPP
