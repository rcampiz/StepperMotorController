/**
 * @file motion_screen.hpp
 * @brief Motion control screen (jog, speed, enable/disable)
 *
 * Interactive field-based UI for motor control:
 *   - Jog CW/CCW with configurable step size
 *   - Speed control (run command)
 *   - Soft stop
 *   - Enable/Disable motor outputs
 *
 * Safety: grays out motion commands when motor is faulted/stalled.
 * All motion goes through Services::Motion (never HAL/registers).
 *
 * No RTOS headers.
 */

#ifndef MOTION_SCREEN_HPP
#define MOTION_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

class LCD;

namespace UI {

class MotionScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    static constexpr uint8_t NUM_FIELDS = 6;
    static constexpr uint8_t FIELD_JOG_CW = 0;
    static constexpr uint8_t FIELD_JOG_CCW = 1;
    static constexpr uint8_t FIELD_JOG_SIZE = 2;
    static constexpr uint8_t FIELD_SPEED = 3;
    static constexpr uint8_t FIELD_STOP = 4;
    static constexpr uint8_t FIELD_ENABLE = 5;

    uint8_t m_selectedField = 0;
    bool m_needsRedraw = true;

    // Jog step sizes
    static constexpr int32_t JOG_SIZES[] = {100, 200, 500, 1000, 5000, 10000};
    static constexpr uint8_t NUM_JOG_SIZES = 6;
    uint8_t m_jogSizeIdx = 3;  // Default 1000

    // Speed
    uint32_t m_speed = 200;  // steps/s
    static constexpr uint32_t SPEED_MIN = 10;
    static constexpr uint32_t SPEED_MAX = 15625;
    static constexpr uint32_t SPEED_STEP = 50;

    bool isFieldEnabled(uint8_t field, bool faulted, bool hiZ) const;
    void renderField(LCD& lcd, uint8_t field, uint16_t y, bool selected,
                     bool faulted, bool hiZ);
    void renderStatus(LCD& lcd);
};

} // namespace UI

#endif // MOTION_SCREEN_HPP
