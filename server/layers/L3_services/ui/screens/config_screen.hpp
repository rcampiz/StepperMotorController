/**
 * @file config_screen.hpp
 * @brief Driver configuration from the joystick: resolution, drive levels, overcurrent, stall,
 *        ramps, idle release, save. Physical values; ranges and steps from the driver's caps.
 *
 * UP/DOWN pick a row, CENTER opens it for editing (LEFT/RIGHT adjust, CENTER closes),
 * LEFT goes back. The Save row writes the configuration to flash.
 */

#ifndef CONFIG_SCREEN_HPP
#define CONFIG_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class ConfigScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Driver Config"; }
    const char* centerHint() const override { return "EDIT"; }

private:
    static constexpr uint8_t NUM_FIELDS = 10;
    static constexpr uint8_t FIELD_STEP_MODE = 0;
    static constexpr uint8_t FIELD_HOLD = 1;     ///< hold current (current mode) or duty (voltage mode)
    static constexpr uint8_t FIELD_RUN = 2;      ///< run current / duty (acc and dec follow it)
    static constexpr uint8_t FIELD_OCD = 3;
    static constexpr uint8_t FIELD_STALL = 4;
    static constexpr uint8_t FIELD_ACC = 5;
    static constexpr uint8_t FIELD_DEC = 6;
    static constexpr uint8_t FIELD_MAXSPD = 7;
    static constexpr uint8_t FIELD_IDLE = 8;     ///< coils to Hi-Z after sitting stopped: off / 1-10 s
    static constexpr uint8_t FIELD_SAVE = 9;

    uint8_t m_selectedField = 0;
    bool m_editing = false;
    bool m_needsRedraw = true;
    bool m_full = true;
    bool m_hintDirty = true;
    uint16_t m_rowsDirty = 0;
    bool m_saved = false;      // "saved" shown on the Save row until the next change

    void renderField(Services::ICanvas& lcd, uint8_t field);
    void adjustField(uint8_t field, int8_t delta);
    void applyToChip();   ///< the edited setting to the motor driver now
    void markRow(uint8_t field) { m_rowsDirty = static_cast<uint16_t>(m_rowsDirty | (1u << field)); }
};

} // namespace UI

#endif // CONFIG_SCREEN_HPP
