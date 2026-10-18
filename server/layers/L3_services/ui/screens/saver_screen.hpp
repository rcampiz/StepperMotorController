/**
 * @file saver_screen.hpp
 * @brief Screen saver: a black panel with a small HUD mark that moves every two seconds,
 *        or nothing at all. Pushed by the DisplayService after a quiet period; the first
 *        joystick move pops it (and is swallowed).
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
    static constexpr uint16_t MARK_W = 56;
    static constexpr uint16_t MARK_H = 36;
    static constexpr uint16_t HOLD_FRAMES = 40;     // 2 s at 20 Hz

    const ScreenCareSettings* m_settings;
    bool m_needsRedraw = true;
    uint16_t m_frames = 0;
    uint16_t m_x = 0, m_y = 0;
    bool m_drawn = false;
    uint32_t m_seed = 0x2545F491u;

    uint32_t nextRandom();
    void drawMark(Services::ICanvas& lcd, uint16_t x, uint16_t y, bool erase);
};

} // namespace UI

#endif // SAVER_SCREEN_HPP
