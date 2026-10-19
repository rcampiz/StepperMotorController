/**
 * @file motion_screen.cpp
 * @brief Motion screen: a live status block, then the action and setting rows.
 */

#include "ui/screens/motion_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/motion_service/motion_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/motion/motion.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {


// Layout: status block (two rows) then the fields, a hint line, the deck
static constexpr uint16_t STATUS_Y  = Theme::CONTENT_Y + 2;
static constexpr uint16_t FIELDS_Y  = Theme::CONTENT_Y + 40;
static constexpr uint16_t HINT_Y    = FIELDS_Y + 8 * Chrome::ROW_H + 4;

static uint16_t fieldY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * Chrome::ROW_H); }

uint32_t MotionScreen::rpmToFullStepsPerSec(uint16_t rpm) const
{
    // The same conversion as MOT:RUN <rpm> RPM: full steps per revolution / 60
    const uint32_t full = Services::motion.stepper.config.getFullStepsPerRev();
    return (static_cast<uint32_t>(rpm) * full + 30u) / 60u;
}

void MotionScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_selectedField = 0;
    m_editing = false;
    m_hintDirty = true;
    m_posText[0] = m_spdText[0] = m_stateText[0] = '\0';
}

bool MotionScreen::isFieldEnabled(uint8_t field, bool faulted) const
{
    if (field == FIELD_STOP || field == FIELD_ENABLE) return true;
    return !faulted;
}

void MotionScreen::renderStatus(Services::ICanvas& lcd, bool force)
{
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    char buf[24];

    if (force) {
        lcd.drawString(Theme::MARGIN + 2, STATUS_Y + 4, "POS", Theme::CYAN, Theme::BG, Theme::FONT_SMALL);
        lcd.drawString(Theme::MARGIN + 2, STATUS_Y + 22, "SPD", Theme::CYAN, Theme::BG, Theme::FONT_SMALL);
        lcd.drawString(150, STATUS_Y + 4, "STATE", Theme::CYAN, Theme::BG, Theme::FONT_SMALL);
    }
    snprintf(buf, sizeof buf, "%ld", static_cast<long>(t.motor.position));
    if (force || strcmp(buf, m_posText) != 0) {
        snprintf(m_posText, sizeof m_posText, "%s", buf);
        Chrome::textBox(lcd, 32, STATUS_Y, 112, 16, 2, 2, buf, Theme::TEXT, Theme::BG);
    }
    {
        // Commanded speed as the motor sees it: full steps/s -> RPM
        const uint16_t full = Services::motion.stepper.config.getFullStepsPerRev();
        const uint32_t rpmX10 = full ? (t.motor.speed * 600u + full / 2) / full : 0;
        snprintf(buf, sizeof buf, "%lu.%lu RPM", static_cast<unsigned long>(rpmX10 / 10),
                 static_cast<unsigned long>(rpmX10 % 10));
    }
    if (force || strcmp(buf, m_spdText) != 0) {
        snprintf(m_spdText, sizeof m_spdText, "%s", buf);
        Chrome::textBox(lcd, 32, STATUS_Y + 18, 112, 16, 2, 2, buf, Theme::TEXT, Theme::BG);
    }
    const char* state; uint16_t color;
    if (t.motor.stalled)   { state = "STALL"; color = Theme::RED; }
    else if (t.motor.hiZ)  { state = "Hi-Z";  color = Theme::DIM; }
    else if (t.motor.busy) { state = "RUN";   color = Theme::GREEN; }
    else                   { state = "HOLD";  color = Theme::CYAN; }
    if (force || strcmp(state, m_stateText) != 0) {
        snprintf(m_stateText, sizeof m_stateText, "%s", state);
        Chrome::textBox(lcd, 150, STATUS_Y + 14, 84, 18, 0, 3, state, color, Theme::BG);
    }
}

void MotionScreen::renderField(Services::ICanvas& lcd, uint8_t field, bool faulted, bool hiZ)
{
    char val[20];
    const char* label = "";
    uint16_t color = 0;
    val[0] = '\0';
    switch (field) {
        case FIELD_SPEED:    label = "Speed";    snprintf(val, sizeof val, "%u RPM", static_cast<unsigned>(m_rpm)); break;
        case FIELD_RUN_CW:   label = "Run CW";   break;
        case FIELD_RUN_CCW:  label = "Run CCW";  break;
        case FIELD_JOG_SIZE: label = "Jog size"; formatEighths(val, sizeof val, m_jogEighths, 0); break;
        case FIELD_JOG_CW:   label = "Jog CW";   break;     // the size is on the row above
        case FIELD_JOG_CCW:  label = "Jog CCW";  break;
        case FIELD_STOP:     label = "STOP";     color = Theme::YELLOW; break;
        case FIELD_ENABLE:   label = hiZ ? "Enable" : "Release (Hi-Z)"; snprintf(val, sizeof val, "%s", hiZ ? "coils off" : "coils on");
                             color = hiZ ? Theme::DIM : Theme::GREEN; break;
        default: break;
    }
    Chrome::RowState st = Chrome::RowState::NORMAL;
    if (!isFieldEnabled(field, faulted)) st = Chrome::RowState::DISABLED;
    else if (field == m_selectedField) st = m_editing ? Chrome::RowState::EDITING : Chrome::RowState::SELECTED;
    Chrome::fieldRow(lcd, fieldY(field), label, val, st, color);
}

void MotionScreen::render(Services::ICanvas& lcd)
{
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    bool faulted = t.motor.stalled;
    bool hiZ = t.motor.hiZ;

    if (hiZ != m_prevHiZ || faulted != m_prevFaulted) {
        m_prevHiZ = hiZ; m_prevFaulted = faulted;
        m_rowsDirty = 0xFFFF;                      // enabled state and the Enable row text
    }
    if (m_full) {
        m_full = false;
        renderStatus(lcd, true);
        m_rowsDirty = 0xFFFF;
        Chrome::drawDeck(lcd, HINT_Y + 14, Theme::CONTENT_BOTTOM);
    } else {
        renderStatus(lcd, false);
    }
    for (uint8_t i = 0; i < NUM_FIELDS; i++) {
        if (m_rowsDirty & (1u << i)) renderField(lcd, i, faulted, hiZ);
    }
    m_rowsDirty = 0;
    if (m_hintDirty) {
        m_hintDirty = false;
        Chrome::hintLine(lcd, HINT_Y, m_editing ? "UP/DOWN or L/R adjust  CENTER done" : "", Theme::DIM);
    }
}

void MotionScreen::adjust(int8_t delta)
{
    if (m_selectedField == FIELD_JOG_SIZE) {
        if (delta < 0 && m_jogEighths > JOG_EIGHTHS_MIN) m_jogEighths--;          // 1/8 rev a step
        if (delta > 0 && m_jogEighths < JOG_EIGHTHS_MAX) m_jogEighths++;
        markRow(FIELD_JOG_SIZE);
    } else if (m_selectedField == FIELD_SPEED) {
        const uint16_t step = (m_rpm < 100) ? 5 : 10;
        if (delta < 0) m_rpm = (m_rpm > RPM_MIN + step) ? static_cast<uint16_t>(m_rpm - step) : RPM_MIN;
        else           m_rpm = (m_rpm + step <= RPM_MAX) ? static_cast<uint16_t>(m_rpm + step) : RPM_MAX;
        markRow(FIELD_SPEED);
    }
}

void MotionScreen::activate(bool hiZ)
{
    switch (m_selectedField) {
        case FIELD_RUN_CW:  Services::Motion::run(rpmToFullStepsPerSec(m_rpm), true);  break;
        case FIELD_RUN_CCW: Services::Motion::run(rpmToFullStepsPerSec(m_rpm), false); break;
        case FIELD_JOG_CW:
        case FIELD_JOG_CCW: {
            // Revolutions to microsteps at the microstep setting in force now
            const int32_t steps = milliRevToMicrosteps(static_cast<int32_t>(m_jogEighths) * 125,
                                                       Services::motion.stepper.config.getMicrostepsPerRev());
            Services::Motion::move(m_selectedField == FIELD_JOG_CW ? steps : -steps);
            break;
        }
        case FIELD_STOP:    Services::Motion::stop(false); break;
        case FIELD_ENABLE:  if (hiZ) Services::Motion::enable(); else Services::Motion::disable(); break;
        default: break;
    }
}

InputResult MotionScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    bool faulted = t.motor.stalled;

    switch (dir) {
        case JoyDirection::UP:
        case JoyDirection::DOWN: {
            if (m_editing) {                         // UP = more, DOWN = less; held, they repeat
                adjust(dir == JoyDirection::UP ? 1 : -1);
                return InputResult::HANDLED;
            }
            uint8_t prev = m_selectedField;
            if (dir == JoyDirection::UP && m_selectedField > 0) m_selectedField--;
            if (dir == JoyDirection::DOWN && m_selectedField < NUM_FIELDS - 1) m_selectedField++;
            markRow(prev); markRow(m_selectedField);
            return InputResult::HANDLED;
        }
        case JoyDirection::LEFT:
            if (m_editing) { adjust(-1); return InputResult::HANDLED; }
            return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:
            if (m_editing) { adjust(1); return InputResult::HANDLED; }
            if (isEditable(m_selectedField)) { m_editing = true; m_hintDirty = true; markRow(m_selectedField); }
            return InputResult::HANDLED;
        case JoyDirection::CENTER:
            if (m_editing) { m_editing = false; m_hintDirty = true; markRow(m_selectedField); return InputResult::HANDLED; }
            if (isEditable(m_selectedField)) { m_editing = true; m_hintDirty = true; markRow(m_selectedField); return InputResult::HANDLED; }
            if (isFieldEnabled(m_selectedField, faulted)) activate(t.motor.hiZ);
            return InputResult::HANDLED;
        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
