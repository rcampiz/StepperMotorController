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
#include <stddef.h>
#include <stdio.h>

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
    char m_posText[24] = {};
    char m_spdText[24] = {};
    char m_stateText[8] = {};
    bool m_prevHiZ = true;
    bool m_prevFaulted = false;

    // Jog size in eighths of a revolution (1/8 .. 10 rev, in 1/8 steps): what a person means
    // by "how far", and independent of the microstep setting; converted to microsteps with
    // the microsteps per revolution in force when the jog is sent (as MOT:MOVE <rev> REV does)
    static constexpr uint8_t JOG_EIGHTHS_MIN = 1;
    static constexpr uint8_t JOG_EIGHTHS_MAX = 80;
    uint8_t m_jogEighths = 8;        // 1 rev
    // Speed in RPM: what a person means by "how fast", and independent of the microstep
    // setting; converted to full steps/s with the configured steps per revolution on use
    uint16_t m_rpm = 90;             // clear of the 55-66 and 105-122 RPM resonance bands
    static constexpr uint16_t RPM_MIN = 5;
    static constexpr uint16_t RPM_MAX = 600;
    uint32_t rpmToFullStepsPerSec(uint16_t rpm) const;

public:
    /// Microsteps for `millirev` thousandths of a revolution at `ustepsPerRev`, rounded
    static int32_t milliRevToMicrosteps(int32_t millirev, uint32_t ustepsPerRev)
    {
        const int64_t num = static_cast<int64_t>(millirev) * static_cast<int64_t>(ustepsPerRev);
        return static_cast<int32_t>(num >= 0 ? (num + 500) / 1000 : (num - 500) / 1000);
    }
    /// Eighths of a revolution as a fraction: "3/8 rev", "1 1/4 rev", "2 rev", with a sign
    /// prefix if given ('+', '-', or 0 for none)
    static void formatEighths(char* out, size_t size, uint8_t eighths, char sign)
    {
        const unsigned whole = eighths / 8u;
        unsigned num = eighths % 8u, den = 8;
        while (num != 0 && (num % 2) == 0) { num /= 2; den /= 2; }
        const char sg[2] = {sign, '\0'};
        if (num == 0) snprintf(out, size, "%s%u rev", sg, whole);
        else if (whole == 0) snprintf(out, size, "%s%u/%u rev", sg, num, den);
        else snprintf(out, size, "%s%u %u/%u rev", sg, whole, num, den);
    }

private:

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
