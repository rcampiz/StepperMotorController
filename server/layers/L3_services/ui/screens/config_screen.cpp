/**
 * @file config_screen.cpp
 * @brief Driver configuration screen implementation
 */

#include "ui/screens/config_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "L3_services/config/config_service.hpp"
#include <stdio.h>

namespace UI {

// Layout (scale=3 medium font: 8px wide, 12px tall)
static constexpr uint8_t TEXT_SCALE = 3;
static constexpr uint16_t MARGIN = 6;
static constexpr uint16_t LINE_H = 15;
static constexpr uint16_t FIELD_H = 18;
static constexpr uint16_t FIELD_PAD = 3;
static constexpr uint16_t FIELDS_START_Y = 28;
static constexpr uint16_t VALUE_X = 56;   // After 7-char labels at 8px

// Colors
static constexpr uint16_t LABEL_COLOR = 0x07FF;
static constexpr uint16_t VALUE_COLOR = 0xFFFF;
static constexpr uint16_t BG_COLOR = 0x0000;
static constexpr uint16_t SEL_BG = 0x001F;
static constexpr uint16_t OK_COLOR = 0x07E0;

// Step mode names
static const char* stepModeNames[] = {
    "Full", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128"
};

void ConfigScreen::onActivate()
{
    m_needsRedraw = true;
    m_selectedField = 0;
    m_saved = false;
}

void ConfigScreen::renderField(LCD& lcd, uint8_t field, uint16_t y, bool selected)
{
    uint16_t fg = VALUE_COLOR;
    uint16_t bg = selected ? SEL_BG : BG_COLOR;
    uint16_t itemW = LCD::WIDTH - 2 * MARGIN;
    char buf[32];

    lcd.fillRect(MARGIN, y, itemW, FIELD_H - 2, bg);

    const auto& cfg = Services::g_motorConfig.getConfig();

    switch (field) {
        case FIELD_STEP_MODE: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Step:", fg, bg, TEXT_SCALE);
            uint8_t mode = cfg.stepMode > 7 ? 7 : cfg.stepMode;
            snprintf(buf, sizeof(buf), "< %s >", stepModeNames[mode]);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_KVAL_HOLD: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "KHold:", fg, bg, TEXT_SCALE);
            auto pct = static_cast<uint16_t>(cfg.kvalHold) * 100 / 256;
            snprintf(buf, sizeof(buf), "< %u%% >", pct);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_KVAL_RUN: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "KRun:", fg, bg, TEXT_SCALE);
            auto pct = static_cast<uint16_t>(cfg.kvalRun) * 100 / 256;
            snprintf(buf, sizeof(buf), "< %u%% >", pct);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_OCD: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "OCD:", fg, bg, TEXT_SCALE);
            uint16_t mA = (static_cast<uint16_t>(cfg.ocdThreshold) + 1) * 375;
            snprintf(buf, sizeof(buf), "< %u mA >", mA);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_STALL: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Stall:", fg, bg, TEXT_SCALE);
            uint16_t mV = (static_cast<uint16_t>(cfg.stallThreshold) + 1) * 3125 / 100;
            snprintf(buf, sizeof(buf), "< %u mV >", mV);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_ACC: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Accel:", fg, bg, TEXT_SCALE);
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.acceleration);
            snprintf(buf, sizeof(buf), "< %lu s/s2 >", (unsigned long)phys);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_DEC: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Decel:", fg, bg, TEXT_SCALE);
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.deceleration);
            snprintf(buf, sizeof(buf), "< %lu s/s2 >", (unsigned long)phys);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_MAXSPD: {
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "MaxSp:", fg, bg, TEXT_SCALE);
            uint32_t phys = Services::Config::maxSpeedRawToPhysical(cfg.maxSpeed);
            snprintf(buf, sizeof(buf), "< %lu s/s >", (unsigned long)phys);
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;
        }

        case FIELD_SAVE:
            if (m_saved) {
                lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Saved!", OK_COLOR, bg, TEXT_SCALE);
            } else {
                lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Save to Flash", fg, bg, TEXT_SCALE);
            }
            break;
    }
}

void ConfigScreen::adjustField(uint8_t field, int8_t delta)
{
    auto& cfg = Services::g_motorConfig.getConfigMutable();
    m_saved = false;

    switch (field) {
        case FIELD_STEP_MODE: {
            auto mode = static_cast<int8_t>(cfg.stepMode) + delta;
            if (mode < 0) mode = 0;
            if (mode > 7) mode = 7;
            Services::g_motorConfig.setStepMode(static_cast<uint8_t>(mode));
            break;
        }

        case FIELD_KVAL_HOLD: {
            auto val = static_cast<int16_t>(cfg.kvalHold) + delta;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            Services::g_motorConfig.setKval(
                static_cast<uint8_t>(val), cfg.kvalRun, cfg.kvalAcc, cfg.kvalDec);
            break;
        }

        case FIELD_KVAL_RUN: {
            auto val = static_cast<int16_t>(cfg.kvalRun) + delta;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            Services::g_motorConfig.setKval(
                cfg.kvalHold, static_cast<uint8_t>(val), cfg.kvalAcc, cfg.kvalDec);
            break;
        }

        case FIELD_OCD: {
            auto val = static_cast<int16_t>(cfg.ocdThreshold) + delta;
            if (val < 0) val = 0;
            if (val > 31) val = 31;
            Services::g_motorConfig.setOcdThreshold(static_cast<uint8_t>(val));
            break;
        }

        case FIELD_STALL: {
            auto val = static_cast<int16_t>(cfg.stallThreshold) + delta;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            Services::g_motorConfig.setStallThreshold(static_cast<uint8_t>(val));
            break;
        }

        case FIELD_ACC: {
            // Adjust by ±50 steps/s^2 in physical units
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.acceleration);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 15) newPhys = 15;        // Min ~1 raw
            if (newPhys > 59590) newPhys = 59590;  // Max 12-bit raw (4095)
            uint16_t raw = Services::Config::accelPhysicalToRaw(static_cast<uint32_t>(newPhys));
            cfg.acceleration = raw;
            break;
        }

        case FIELD_DEC: {
            uint32_t phys = Services::Config::accelRawToPhysical(cfg.deceleration);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 15) newPhys = 15;
            if (newPhys > 59590) newPhys = 59590;
            uint16_t raw = Services::Config::accelPhysicalToRaw(static_cast<uint32_t>(newPhys));
            cfg.deceleration = raw;
            break;
        }

        case FIELD_MAXSPD: {
            uint32_t phys = Services::Config::maxSpeedRawToPhysical(cfg.maxSpeed);
            auto newPhys = static_cast<int32_t>(phys) + (delta * 50);
            if (newPhys < 16) newPhys = 16;         // Min ~1 raw
            if (newPhys > 15625) newPhys = 15625;   // Max 10-bit raw (1023)
            uint16_t raw = Services::Config::maxSpeedPhysicalToRaw(static_cast<uint32_t>(newPhys));
            cfg.maxSpeed = raw;
            break;
        }

        default:
            break;
    }
}

void ConfigScreen::render(LCD& lcd)
{
    uint16_t y = MARGIN;
    lcd.drawString(MARGIN, y, "Driver Config", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H + 2;
    lcd.drawHLine(MARGIN, y, LCD::WIDTH - 2 * MARGIN, LABEL_COLOR);
    y = FIELDS_START_Y;

    for (uint8_t i = 0; i < NUM_FIELDS; i++) {
        renderField(lcd, i, y, (i == m_selectedField));
        y += FIELD_H;
    }
}

InputResult ConfigScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    switch (dir) {
        case JoyDirection::UP:
            if (m_selectedField > 0) {
                m_selectedField--;
                m_saved = false;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_selectedField < NUM_FIELDS - 1) {
                m_selectedField++;
                m_saved = false;
            }
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (m_selectedField <= FIELD_MAXSPD) {
                adjustField(m_selectedField, -1);
                return InputResult::HANDLED;
            }
            return InputResult::EXIT_SCREEN;

        case JoyDirection::RIGHT:
            if (m_selectedField <= FIELD_MAXSPD) {
                adjustField(m_selectedField, 1);
                return InputResult::HANDLED;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_selectedField == FIELD_SAVE) {
                m_saved = Services::g_motorConfig.saveToFlash();
            }
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
