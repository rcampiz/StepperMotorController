/**
 * @file config_screen.hpp
 * @brief Driver configuration screen
 *
 * Edit powerSTEP01 parameters: step mode, KVAL, OCD, stall thresholds.
 * Changes applied to RAM immediately, saved to flash on explicit Save.
 * All changes go through Services::motion.motorConfig.
 *
 * No RTOS headers.
 */

#ifndef CONFIG_SCREEN_HPP
#define CONFIG_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class ConfigScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(Harness::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

private:
    static constexpr uint8_t NUM_FIELDS = 9;
    static constexpr uint8_t FIELD_STEP_MODE = 0;
    static constexpr uint8_t FIELD_KVAL_HOLD = 1;
    static constexpr uint8_t FIELD_KVAL_RUN = 2;
    static constexpr uint8_t FIELD_OCD = 3;
    static constexpr uint8_t FIELD_STALL = 4;
    static constexpr uint8_t FIELD_ACC = 5;
    static constexpr uint8_t FIELD_DEC = 6;
    static constexpr uint8_t FIELD_MAXSPD = 7;
    static constexpr uint8_t FIELD_SAVE = 8;

    uint8_t m_selectedField = 0;
    bool m_needsRedraw = true;
    bool m_saved = false;  // Show "Saved!" feedback briefly

    void renderField(Harness::ICanvas& lcd, uint8_t field, uint16_t y, bool selected);
    void adjustField(uint8_t field, int8_t delta);
};

} // namespace UI

#endif // CONFIG_SCREEN_HPP
