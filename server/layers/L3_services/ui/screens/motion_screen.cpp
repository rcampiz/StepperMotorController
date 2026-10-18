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
#include <stdio.h>
#include <string.h>

namespace UI {

constexpr int32_t MotionScreen::JOG_SIZES[];

// Layout: status block (two rows) then the fields, a hint line, the deck
static constexpr uint16_t STATUS_Y  = Theme::CONTENT_Y + 2;
static constexpr uint16_t FIELDS_Y  = Theme::CONTENT_Y + 40;
static constexpr uint16_t HINT_Y    = FIELDS_Y + 8 * Chrome::ROW_H + 4;

static uint16_t fieldY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * Chrome::ROW_H); }

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
    char buf[12];

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
    snprintf(buf, sizeof buf, "%lu st/s", static_cast<unsigned long>(t.motor.speed));
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
    char val[14];
    const char* label = "";
    uint16_t color = 0;
    val[0] = '\0';
    switch (field) {
        case FIELD_SPEED:    label = "Speed";    snprintf(val, sizeof val, "%lu st/s", static_cast<unsigned long>(m_speed)); break;
        case FIELD_RUN_CW:   label = "Run CW";   break;
        case FIELD_RUN_CCW:  label = "Run CCW";  break;
        case FIELD_JOG_SIZE: label = "Jog size"; snprintf(val, sizeof val, "%ld", static_cast<long>(JOG_SIZES[m_jogSizeIdx])); break;
        case FIELD_JOG_CW:   label = "Jog CW";   snprintf(val, sizeof val, "+%ld", static_cast<long>(JOG_SIZES[m_jogSizeIdx])); break;
        case FIELD_JOG_CCW:  label = "Jog CCW";  snprintf(val, sizeof val, "-%ld", static_cast<long>(JOG_SIZES[m_jogSizeIdx])); break;
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
        Chrome::hintLine(lcd, HINT_Y, m_editing ? "LEFT/RIGHT adjust   CENTER done" : "", Theme::DIM);
    }
}

void MotionScreen::adjust(int8_t delta)
{
    if (m_selectedField == FIELD_JOG_SIZE) {
        if (delta < 0 && m_jogSizeIdx > 0) m_jogSizeIdx--;
        if (delta > 0 && m_jogSizeIdx < NUM_JOG_SIZES - 1) m_jogSizeIdx++;
        markRow(FIELD_JOG_SIZE); markRow(FIELD_JOG_CW); markRow(FIELD_JOG_CCW);
    } else if (m_selectedField == FIELD_SPEED) {
        uint32_t step = (m_speed < 1000) ? 50 : 250;
        if (delta < 0) m_speed = (m_speed > SPEED_MIN + step) ? m_speed - step : SPEED_MIN;
        else           m_speed = (m_speed + step <= SPEED_MAX) ? m_speed + step : SPEED_MAX;
        markRow(FIELD_SPEED);
    }
}

void MotionScreen::activate(bool hiZ)
{
    switch (m_selectedField) {
        case FIELD_RUN_CW:  Services::Motion::run(m_speed, true);  break;
        case FIELD_RUN_CCW: Services::Motion::run(m_speed, false); break;
        case FIELD_JOG_CW:  Services::Motion::move(JOG_SIZES[m_jogSizeIdx]);  break;
        case FIELD_JOG_CCW: Services::Motion::move(-JOG_SIZES[m_jogSizeIdx]); break;
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
            if (m_editing) { m_editing = false; m_hintDirty = true; }
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
