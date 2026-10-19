/**
 * @file config_screen.cpp
 * @brief Driver config rows with an edit mode; rows repaint only when they change.
 */

#include "ui/screens/config_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include <stdio.h>

namespace UI {

// Layout: the rows, the key-hint line under the last one, the row help, the deck
static constexpr uint8_t  ROW_COUNT = 10;
static constexpr uint16_t FIELDS_Y = Theme::CONTENT_Y + 4;
static constexpr uint16_t HINT_Y   = FIELDS_Y + ROW_COUNT * Chrome::ROW_H + 4;
static constexpr uint16_t HELP_Y   = HINT_Y + Chrome::HELP_LINE_H;
static constexpr uint16_t DECK_Y   = HELP_Y + Chrome::HELP_H + 4;
static_assert(DECK_Y + 12 < Theme::CONTENT_BOTTOM, "the rows, hint and help fit above the footer");

static uint16_t fieldY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * Chrome::ROW_H); }

// What each row does, in plain words, in row order (Chrome::HelpArea: two lines of 38
// characters, wrapped at spaces or at a '\n'). The second text, when there is one, is the
// row's meaning in voltage mode. No '~': the small font draws it as an arrow.
struct RowHelp { const char* text; const char* voltageMode; };
static constexpr RowHelp HELP[ROW_COUNT] = {
    {"Microsteps per full step.\nCurrent mode runs 1/16 at most.", nullptr},
    {"Coil current when stopped (amps).", "Drive duty when stopped (voltage mode)."},
    {"Coil current while moving (amps).", "Drive duty while moving (voltage mode)."},
    {"Overcurrent flag: switch voltage limit (amps: estimate). Not motor current.", nullptr},
    {"Stall detection sensitivity.", nullptr},
    {"Ramp-up rate (full steps/s^2).", nullptr},
    {"Ramp-down rate (full steps/s^2).", nullptr},
    {"Speed ceiling for moves (full steps/s).", nullptr},
    {"Coils off after the motor sits idle this long.", nullptr},
    {"Keep these settings after power off.", nullptr},
};

const char* ConfigScreen::helpText(uint8_t field, bool currentMode)
{
    static_assert(NUM_FIELDS == ROW_COUNT, "the layout and the help table count every row");
    if (field >= ROW_COUNT) return "";
    const RowHelp& h = HELP[field];
    return (!currentMode && h.voltageMode != nullptr) ? h.voltageMode : h.text;
}

void ConfigScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_selectedField = 0;
    m_editing = false;
    m_saved = false;
    m_hintDirty = true;
}

// Idle release choices (ms): off, then how long the motor may sit stopped and energised
static constexpr uint16_t IDLE_CHOICES_MS[] = { 0, 1000, 2000, 3000, 5000, 10000 };
static constexpr uint8_t IDLE_CHOICE_COUNT = 6;

static uint8_t idleIndex(uint16_t ms)
{
    for (uint8_t i = 0; i < IDLE_CHOICE_COUNT; i++) if (IDLE_CHOICES_MS[i] == ms) return i;
    for (uint8_t i = 1; i < IDLE_CHOICE_COUNT; i++) if (IDLE_CHOICES_MS[i] >= ms) return i;   // nearest above
    return IDLE_CHOICE_COUNT - 1;
}

/// Amps as "1.33 A" (truncated to the hundredth, as the screen always showed currents)
static void formatAmps(char* out, size_t size, float amps)
{
    const unsigned ma = (amps > 0.0f) ? static_cast<unsigned>(amps * 1000.0f + 0.5f) : 0u;
    snprintf(out, size, "%u.%02u A", ma / 1000u, (ma % 1000u) / 10u);
}

/// A physical value shown whole (the screen's steps are whole units; tolerant of float error)
static unsigned long whole(float v)
{
    return (v > 0.0f) ? static_cast<unsigned long>(v + 0.001f) : 0UL;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void ConfigScreen::renderField(Services::ICanvas& lcd, uint8_t field)
{
    // What the driver puts in force (its resolution cap and grid), in physical units
    const auto& sc = Services::motion.stepper.config;
    const Harness::DriverCaps& caps = sc.caps();
    const Harness::DriverSettings eff = sc.getEffectiveSettings();
    const bool cm = sc.isCurrentMode();
    char val[24];
    const char* label = "";
    uint16_t color = 0;
    val[0] = '\0';
    switch (field) {
        case FIELD_STEP_MODE:
            label = "Step mode";
            if (eff.microsteps <= 1) snprintf(val, sizeof val, "Full");
            else snprintf(val, sizeof val, "1/%u", static_cast<unsigned>(eff.microsteps));
            break;
        // Current mode drives by current (amps), voltage mode by duty: show what is in force
        case FIELD_HOLD:
        case FIELD_RUN: {
            const bool hold = (field == FIELD_HOLD);
            if (!caps.hasMode(eff.mode)) {
                label = hold ? "Hold" : "Run";
                snprintf(val, sizeof val, "--");
            } else if (cm) {
                label = hold ? "Hold curr" : "Run curr";
                formatAmps(val, sizeof val, hold ? eff.currentA.hold : eff.currentA.run);
            } else {
                label = hold ? "Hold duty" : "Run duty";
                const float d = hold ? eff.duty.hold : eff.duty.run;
                snprintf(val, sizeof val, "%lu%%", whole(d * 100.0f));
            }
            break;
        }
        case FIELD_OCD:
            label = "OCD";
            if (caps.overcurrentOhms > 0.0f) {
                // The driver senses a switch voltage: show it, and the current it stands for
                // on this board (an estimate from the switch's resistance)
                const unsigned cv = static_cast<unsigned>(eff.overcurrentA * caps.overcurrentOhms * 100.0f + 0.5f);
                snprintf(val, sizeof val, "%u.%02uV ~%luA", cv / 100u, cv % 100u, whole(eff.overcurrentA + 0.5f));
            } else {
                formatAmps(val, sizeof val, eff.overcurrentA);
            }
            break;
        case FIELD_STALL:
            label = "Stall";
            if (caps.stallDetect) snprintf(val, sizeof val, "%lu%%", whole(eff.stallThreshold01 * 100.0f + 0.5f));
            else snprintf(val, sizeof val, "n/a");
            break;
        case FIELD_ACC:       label = "Accel";      snprintf(val, sizeof val, "%lu", whole(eff.accelFsps2)); break;
        case FIELD_DEC:       label = "Decel";      snprintf(val, sizeof val, "%lu", whole(eff.decelFsps2)); break;
        case FIELD_MAXSPD:    label = "Max speed";  snprintf(val, sizeof val, "%lu", whole(eff.maxSpeedFsps)); break;
        case FIELD_IDLE: {
            label = "Idle release";
            const uint16_t ms = sc.getIdleReleaseMs();
            if (ms == 0) snprintf(val, sizeof val, "off");
            else snprintf(val, sizeof val, "after %u s", static_cast<unsigned>(ms / 1000u));
            break;
        }
        case FIELD_SAVE:      label = "Save to flash"; snprintf(val, sizeof val, "%s", m_saved ? "saved" : ""); color = Theme::GREEN; break;
        default: break;
    }
    Chrome::RowState st = Chrome::RowState::NORMAL;
    if (field == m_selectedField) st = m_editing ? Chrome::RowState::EDITING : Chrome::RowState::SELECTED;
    Chrome::fieldRow(lcd, fieldY(field), label, val, st, color);
}

void ConfigScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        m_rowsDirty = 0xFFFF;
        m_help.invalidate();
        Chrome::drawDeck(lcd, DECK_Y, Theme::CONTENT_BOTTOM);
    }
    for (uint8_t i = 0; i < NUM_FIELDS; i++) {
        if (m_rowsDirty & (1u << i)) renderField(lcd, i);
    }
    m_rowsDirty = 0;
    if (m_hintDirty) {
        m_hintDirty = false;
        Chrome::hintLine(lcd, HINT_Y, m_editing ? "LEFT/RIGHT adjust   CENTER done"
                                                : "Changes apply now; Save keeps them", Theme::DIM);
    }
    // Paints only when the selection (or the drive mode a row means) changed
    m_help.show(lcd, HELP_Y, helpText(m_selectedField, Services::motion.stepper.config.isCurrentMode()));
}

void ConfigScreen::adjustField(uint8_t field, int8_t delta)
{
    // Edits start from what is in force and move by the driver's own steps (its caps)
    auto& sc = Services::motion.stepper.config;
    const Harness::DriverCaps& caps = sc.caps();
    const Harness::DriverSettings eff = sc.getEffectiveSettings();
    Harness::DriverSettings s = sc.getDriverSettings();
    const float d = static_cast<float>(delta);
    if (m_saved) { m_saved = false; markRow(FIELD_SAVE); }
    switch (field) {
        case FIELD_STEP_MODE: {
            // One binary step at a time, within what the drive mode offers
            const int maxK = Harness::microstepsLog2(caps.maxMicrosteps(eff.mode));
            int k = Harness::microstepsLog2(eff.microsteps) + delta;
            if (k < 0) k = 0;
            if (k > maxK) k = maxK;
            sc.setMicrosteps(static_cast<uint16_t>(1u << k));
            break;
        }
        case FIELD_HOLD:
            if (!caps.hasMode(eff.mode)) break;
            if (sc.isCurrentMode()) {                          // one current step a press
                s.currentA.hold = clampf(eff.currentA.hold + d * caps.currentStepA,
                                         caps.currentStepA, caps.maxCurrentA);
            } else {                                           // three duty steps a press
                s.duty.hold = clampf(eff.duty.hold + d * 3.0f * caps.dutyStep, 0.0f, 1.0f - caps.dutyStep);
            }
            sc.setDriverSettings(s);
            break;
        case FIELD_RUN:
            if (!caps.hasMode(eff.mode)) break;
            if (sc.isCurrentMode()) {                          // run, with acc/dec kept 3 steps above
                const float run = clampf(eff.currentA.run + d * caps.currentStepA, caps.currentStepA,
                                         caps.maxCurrentA - 3.0f * caps.currentStepA);
                s.currentA.run = run;
                s.currentA.acc = s.currentA.dec = run + 3.0f * caps.currentStepA;
            } else {
                s.duty.run = clampf(eff.duty.run + d * 3.0f * caps.dutyStep, 0.0f, 1.0f - caps.dutyStep);
            }
            sc.setDriverSettings(s);
            break;
        case FIELD_OCD:
            s.overcurrentA = clampf(eff.overcurrentA + d * caps.overcurrentStepA,
                                    caps.overcurrentMinA, caps.overcurrentMaxA);
            sc.setDriverSettings(s);
            break;
        case FIELD_STALL:
            if (!caps.stallDetect) break;
            s.stallThreshold01 = clampf(eff.stallThreshold01 + d * caps.stallStep01, 0.0f, 1.0f);
            sc.setDriverSettings(s);
            break;
        case FIELD_ACC:
        case FIELD_DEC: {
            // 50 full steps/s^2 a press, from the whole value shown
            const float base = static_cast<float>(whole(field == FIELD_ACC ? eff.accelFsps2 : eff.decelFsps2));
            const float lo = caps.accelMinFsps2 > 15.0f ? caps.accelMinFsps2 : 15.0f;
            const float v = static_cast<float>(whole(clampf(base + d * 50.0f, lo, caps.accelMaxFsps2)));
            if (field == FIELD_ACC) s.accelFsps2 = v; else s.decelFsps2 = v;
            sc.setDriverSettings(s);
            break;
        }
        case FIELD_IDLE: {
            auto i = static_cast<int8_t>(idleIndex(sc.getIdleReleaseMs()) + delta);
            if (i < 0) i = 0;
            if (i >= IDLE_CHOICE_COUNT) i = IDLE_CHOICE_COUNT - 1;
            sc.setIdleReleaseMs(IDLE_CHOICES_MS[i]);
            break;
        }
        case FIELD_MAXSPD: {
            // 50 full steps/s a press, from the whole value shown, up to the driver's ceiling
            const float base = static_cast<float>(whole(eff.maxSpeedFsps));
            s.maxSpeedFsps = static_cast<float>(whole(clampf(base + d * 50.0f, 16.0f, caps.maxSpeedLimitFsps)));
            sc.setDriverSettings(s);
            break;
        }
        default:
            break;
    }
    markRow(field);
}

void ConfigScreen::applyToChip()
{
    // The edited value takes effect now (a Motor-task job, as DRV:CFG:APPLY); Save keeps it
    // over a power cycle. Idle release needs no chip write: the Motor task reads it each pass.
    Services::g_motorService.applyConfig();
}

InputResult ConfigScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (dir) {
        case JoyDirection::UP:
        case JoyDirection::DOWN: {
            if (m_editing) { m_editing = false; m_hintDirty = true; applyToChip(); }
            uint8_t prev = m_selectedField;
            if (dir == JoyDirection::UP && m_selectedField > 0) m_selectedField--;
            if (dir == JoyDirection::DOWN && m_selectedField < NUM_FIELDS - 1) m_selectedField++;
            markRow(prev); markRow(m_selectedField);
            return InputResult::HANDLED;
        }
        case JoyDirection::LEFT:
            if (m_editing) { adjustField(m_selectedField, -1); return InputResult::HANDLED; }
            return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:
            if (m_editing) { adjustField(m_selectedField, 1); return InputResult::HANDLED; }
            if (m_selectedField != FIELD_SAVE) { m_editing = true; m_hintDirty = true; markRow(m_selectedField); }
            return InputResult::HANDLED;
        case JoyDirection::CENTER:
            if (m_editing) { m_editing = false; m_hintDirty = true; markRow(m_selectedField); applyToChip(); return InputResult::HANDLED; }
            if (m_selectedField == FIELD_SAVE) {
                m_saved = Services::motion.configStore.saveToFlash();
                markRow(FIELD_SAVE);
                return InputResult::HANDLED;
            }
            m_editing = true; m_hintDirty = true; markRow(m_selectedField);
            return InputResult::HANDLED;
        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
