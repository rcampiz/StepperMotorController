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
#include "L3_services/config/config_service/config_service.hpp"
#include <stdio.h>

namespace UI {

static constexpr uint16_t FIELDS_Y = Theme::CONTENT_Y + 4;
static constexpr uint16_t HINT_Y   = FIELDS_Y + 9 * Chrome::ROW_H + 4;

static const char* const STEP_MODE_NAMES[] = {"Full", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128"};

static uint16_t fieldY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * Chrome::ROW_H); }

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

/// TVAL units as amps, "1.33 A"
static void formatAmps(char* out, size_t size, uint8_t tval)
{
    const unsigned ma = static_cast<unsigned>(tval) * Services::MotorConfigManager::TVAL_MA_PER_UNIT;
    snprintf(out, size, "%u.%02u A", ma / 1000u, (ma % 1000u) / 10u);
}

void ConfigScreen::renderField(Services::ICanvas& lcd, uint8_t field)
{
    const auto& cfg = Services::motion.stepper.config.getConfig();
    char val[14];
    const char* label = "";
    uint16_t color = 0;
    val[0] = '\0';
    switch (field) {
        case FIELD_STEP_MODE: label = "Step mode"; snprintf(val, sizeof val, "%s", STEP_MODE_NAMES[cfg.stepMode > 7 ? 7 : cfg.stepMode]); break;
        // Current mode drives by TVAL (amps), voltage mode by KVAL (duty): show what is in force
        case FIELD_KVAL_HOLD:
            if (Services::motion.stepper.config.isCurrentMode()) {
                label = "Hold curr"; formatAmps(val, sizeof val, Services::motion.stepper.config.getTvalHold());
            } else {
                label = "KVAL hold"; snprintf(val, sizeof val, "%u%%", static_cast<unsigned>(cfg.kvalHold) * 100 / 256);
            }
            break;
        case FIELD_KVAL_RUN:
            if (Services::motion.stepper.config.isCurrentMode()) {
                label = "Run curr"; formatAmps(val, sizeof val, Services::motion.stepper.config.getTvalRun());
            } else {
                label = "KVAL run"; snprintf(val, sizeof val, "%u%%", static_cast<unsigned>(cfg.kvalRun) * 100 / 256);
            }
            break;
        case FIELD_OCD:       label = "OCD";        snprintf(val, sizeof val, "%u mA", (static_cast<unsigned>(cfg.ocdThreshold) + 1) * 375); break;
        case FIELD_STALL:     label = "Stall";      snprintf(val, sizeof val, "%u mV", (static_cast<unsigned>(cfg.stallThreshold) + 1) * 3125 / 100); break;
        case FIELD_ACC:       label = "Accel";      snprintf(val, sizeof val, "%lu", static_cast<unsigned long>(Services::Config::accelRawToPhysical(cfg.acceleration))); break;
        case FIELD_DEC:       label = "Decel";      snprintf(val, sizeof val, "%lu", static_cast<unsigned long>(Services::Config::accelRawToPhysical(cfg.deceleration))); break;
        case FIELD_MAXSPD:    label = "Max speed";  snprintf(val, sizeof val, "%lu", static_cast<unsigned long>(Services::Config::maxSpeedRawToPhysical(cfg.maxSpeed))); break;
        case FIELD_IDLE: {
            label = "Idle release";
            const uint16_t ms = Services::motion.stepper.config.getIdleReleaseMs();
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
        Chrome::drawDeck(lcd, HINT_Y + 14, Theme::CONTENT_BOTTOM);
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
}

void ConfigScreen::adjustField(uint8_t field, int8_t delta)
{
    auto& cfg = Services::motion.stepper.config.getConfigMutable();
    if (m_saved) { m_saved = false; markRow(FIELD_SAVE); }
    switch (field) {
        case FIELD_STEP_MODE: {
            auto mode = static_cast<int8_t>(cfg.stepMode) + delta;
            if (mode < 0) mode = 0;
            if (mode > 7) mode = 7;
            Services::motion.stepper.config.setStepMode(static_cast<uint8_t>(mode));
            break;
        }
        case FIELD_KVAL_HOLD: {
            auto& sc = Services::motion.stepper.config;
            if (sc.isCurrentMode()) {                          // one TVAL unit (78 mA) a step
                auto t = static_cast<int16_t>(sc.getTvalHold()) + delta;
                if (t < 1) t = 1;
                if (t > 127) t = 127;
                sc.setTval(static_cast<uint8_t>(t), sc.getTvalRun(), sc.getTvalAcc(), sc.getTvalDec());
                break;
            }
            auto val = static_cast<int16_t>(cfg.kvalHold) + delta * 3;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            Services::motion.stepper.config.setKval(static_cast<uint8_t>(val), cfg.kvalRun, cfg.kvalAcc, cfg.kvalDec);
            break;
        }
        case FIELD_KVAL_RUN: {
            auto& sc = Services::motion.stepper.config;
            if (sc.isCurrentMode()) {                          // run, with acc/dec kept 3 units above
                auto t = static_cast<int16_t>(sc.getTvalRun()) + delta;
                if (t < 1) t = 1;
                if (t > 124) t = 124;
                const auto ad = static_cast<uint8_t>(t + 3);
                sc.setTval(sc.getTvalHold(), static_cast<uint8_t>(t), ad, ad);
                break;
            }
            auto val = static_cast<int16_t>(cfg.kvalRun) + delta * 3;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            Services::motion.stepper.config.setKval(cfg.kvalHold, static_cast<uint8_t>(val), cfg.kvalAcc, cfg.kvalDec);
            break;
        }
        case FIELD_OCD: {
            auto val = static_cast<int16_t>(cfg.ocdThreshold) + delta;
            if (val < 0) val = 0;
            if (val > 31) val = 31;
            Services::motion.stepper.config.setOcdThreshold(static_cast<uint8_t>(val));
            break;
        }
        case FIELD_STALL: {
            auto val = static_cast<int16_t>(cfg.stallThreshold) + delta;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            Services::motion.stepper.config.setStallThreshold(static_cast<uint8_t>(val));
            break;
        }
        case FIELD_ACC: {
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.acceleration);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 15) newPhys = 15;
            if (newPhys > 59590) newPhys = 59590;
            cfg.acceleration = Services::Config::accelPhysicalToRaw(static_cast<uint32_t>(newPhys));
            break;
        }
        case FIELD_DEC: {
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.deceleration);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 15) newPhys = 15;
            if (newPhys > 59590) newPhys = 59590;
            cfg.deceleration = Services::Config::accelPhysicalToRaw(static_cast<uint32_t>(newPhys));
            break;
        }
        case FIELD_IDLE: {
            auto i = static_cast<int8_t>(idleIndex(Services::motion.stepper.config.getIdleReleaseMs()) + delta);
            if (i < 0) i = 0;
            if (i >= IDLE_CHOICE_COUNT) i = IDLE_CHOICE_COUNT - 1;
            Services::motion.stepper.config.setIdleReleaseMs(IDLE_CHOICES_MS[i]);
            break;
        }
        case FIELD_MAXSPD: {
            uint32_t phys = Services::Config::maxSpeedRawToPhysical(cfg.maxSpeed);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 16) newPhys = 16;
            if (newPhys > 15625) newPhys = 15625;
            cfg.maxSpeed = Services::Config::maxSpeedPhysicalToRaw(static_cast<uint32_t>(newPhys));
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
