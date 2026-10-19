/**
 * @file screen_care_screen.hpp
 * @brief Screen Care: saver delay and style, recovery length, and a row that starts the
 *        recovery run. The DisplayService owns the settings and starts the run.
 */

#ifndef SCREEN_CARE_SCREEN_HPP
#define SCREEN_CARE_SCREEN_HPP

#include "ui/screen.hpp"
#include "ui/screen_care.hpp"
#include <stdint.h>

namespace UI {

class ScreenCareScreen : public IScreen {
public:
    using RunRecovery = void (*)();

    ScreenCareScreen(ScreenCareSettings* settings, RunRecovery run) : m_settings(settings), m_run(run) {}
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Screen Care"; }
    const char* centerHint() const override { return "EDIT/RUN"; }

private:
    static constexpr uint8_t NUM_FIELDS = 5;
    static constexpr uint8_t FIELD_SAVER_AFTER = 0;
    static constexpr uint8_t FIELD_SLEEP_AFTER = 1;
    static constexpr uint8_t FIELD_SAVER_STYLE = 2;
    static constexpr uint8_t FIELD_RECOVERY_MIN = 3;
    static constexpr uint8_t FIELD_RUN = 4;

    ScreenCareSettings* m_settings;
    RunRecovery m_run;
    uint8_t m_selectedField = 0;
    bool m_editing = false;
    bool m_needsRedraw = true;
    bool m_full = true;
    bool m_hintDirty = true;
    uint16_t m_rowsDirty = 0;

    void renderField(Services::ICanvas& lcd, uint8_t field);
    void adjust(int8_t delta);
    void markRow(uint8_t field) { m_rowsDirty = static_cast<uint16_t>(m_rowsDirty | (1u << field)); }
};

} // namespace UI

#endif // SCREEN_CARE_SCREEN_HPP
