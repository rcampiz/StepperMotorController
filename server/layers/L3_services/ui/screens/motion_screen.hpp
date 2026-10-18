/**
 * @file motion_screen.hpp
 * @brief Local motion control: speed, run, jog, stop, enable, from the joystick.
 *
 * Navigation like every list screen: UP/DOWN pick a row, CENTER does the row or opens an
 * editable one (LEFT/RIGHT then adjust, CENTER closes), LEFT goes back.
 */

#ifndef MOTION_SCREEN_HPP
#define MOTION_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

namespace UI {

class MotionScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Motion"; }
    const char* centerHint() const override { return "DO/EDIT"; }

private:
    static constexpr uint8_t NUM_FIELDS = 8;
    static constexpr uint8_t FIELD_SPEED = 0;     // editable
    static constexpr uint8_t FIELD_RUN_CW = 1;
    static constexpr uint8_t FIELD_RUN_CCW = 2;
    static constexpr uint8_t FIELD_JOG_SIZE = 3;  // editable
    static constexpr uint8_t FIELD_JOG_CW = 4;
    static constexpr uint8_t FIELD_JOG_CCW = 5;
    static constexpr uint8_t FIELD_STOP = 6;
    static constexpr uint8_t FIELD_ENABLE = 7;

    uint8_t m_selectedField = 0;
    bool m_editing = false;
    bool m_needsRedraw = true;
    bool m_full = true;
    uint16_t m_rowsDirty = 0;        // bit per field
    bool m_hintDirty = true;

    // Live status cache, so a cell is repainted only when its text changes
    char m_posText[12] = {};
    char m_spdText[12] = {};
    char m_stateText[8] = {};
    bool m_prevHiZ = true;
    bool m_prevFaulted = false;

    static constexpr int32_t JOG_SIZES[] = {100, 200, 500, 1000, 5000, 10000};
    static constexpr uint8_t NUM_JOG_SIZES = 6;
    uint8_t m_jogSizeIdx = 3;        // 1000
    uint32_t m_speed = 200;          // steps/s
    static constexpr uint32_t SPEED_MIN = 10;
    static constexpr uint32_t SPEED_MAX = 15625;

    static bool isEditable(uint8_t field) { return field == FIELD_SPEED || field == FIELD_JOG_SIZE; }
    bool isFieldEnabled(uint8_t field, bool faulted) const;
    void adjust(int8_t delta);
    void activate(bool hiZ);
    void renderField(Services::ICanvas& lcd, uint8_t field, bool faulted, bool hiZ);
    void renderStatus(Services::ICanvas& lcd, bool force);
    void markRow(uint8_t field) { m_rowsDirty = static_cast<uint16_t>(m_rowsDirty | (1u << field)); }
};

} // namespace UI

#endif // MOTION_SCREEN_HPP
