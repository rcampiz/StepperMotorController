/**
 * @file recovery_screen.hpp
 * @brief Image-retention recovery: the whole panel cycles through full-scale colours for a
 *        set number of minutes, then the screen pops itself. Any joystick move ends it early.
 */

#ifndef RECOVERY_SCREEN_HPP
#define RECOVERY_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class RecoveryScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::IMAGE; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

    void setMinutes(uint8_t minutes) { m_minutes = minutes; }
    bool finished() const { return m_framesLeft == 0; }

private:
    static constexpr uint16_t STEP_FRAMES = 40;     // 2 s per colour at 20 Hz
    bool m_needsRedraw = true;
    uint8_t m_minutes = 5;
    uint32_t m_framesLeft = 0;
    uint8_t m_step = 0;
    uint16_t m_frames = 0;
};

} // namespace UI

#endif // RECOVERY_SCREEN_HPP
