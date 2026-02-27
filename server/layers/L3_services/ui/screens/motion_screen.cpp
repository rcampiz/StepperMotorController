/**
 * @file motion_screen.cpp
 * @brief Motion control screen implementation
 */

#include "ui/screens/motion_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/motion/motion_service.hpp"
#include "L2_protocol/telemetry.hpp"
#include <stdio.h>

namespace UI {

// Static member definition
constexpr int32_t MotionScreen::JOG_SIZES[];

// Layout (scale=3 medium font: 8px wide, 12px tall)
static constexpr uint8_t TEXT_SCALE = 3;
static constexpr uint16_t MARGIN = 6;
static constexpr uint16_t LINE_H = 15;
static constexpr uint16_t FIELD_H = 18;
static constexpr uint16_t FIELD_PAD = 3;
static constexpr uint16_t STATUS_Y = 4;
static constexpr uint16_t FIELDS_START_Y = 58;
static constexpr uint16_t VALUE_X = 80;   // After labels like "Jog CW:"

// Colors
static constexpr uint16_t LABEL_COLOR = 0x07FF;   // Cyan
static constexpr uint16_t VALUE_COLOR = 0xFFFF;    // White
static constexpr uint16_t BG_COLOR = 0x0000;       // Black
static constexpr uint16_t SEL_BG = 0x001F;         // Blue
static constexpr uint16_t DISABLED_COLOR = 0x8410;  // Gray
static constexpr uint16_t OK_COLOR = 0x07E0;       // Green
static constexpr uint16_t FAULT_COLOR = 0xF800;    // Red

void MotionScreen::onActivate()
{
    m_needsRedraw = true;
    m_selectedField = 0;
}

bool MotionScreen::isFieldEnabled(uint8_t field, bool faulted, bool hiZ) const
{
    if (field == FIELD_STOP || field == FIELD_ENABLE) {
        return true;
    }
    return !faulted;
}

void MotionScreen::renderStatus(LCD& lcd)
{
    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();
    char buf[32];
    uint16_t y = STATUS_Y;

    lcd.drawString(MARGIN, y, "Motion", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H + 2;
    lcd.drawHLine(MARGIN, y, LCD::WIDTH - 2 * MARGIN, LABEL_COLOR);
    y += 4;

    // Position
    lcd.drawString(MARGIN, y, "Pos:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    snprintf(buf, sizeof(buf), "%-10ld", static_cast<long>(telem.motor.position));
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Speed
    lcd.drawString(MARGIN, y, "Spd:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    snprintf(buf, sizeof(buf), "%-10lu", static_cast<unsigned long>(telem.motor.speed));
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);

    // State indicator (right side)
    const char* state;
    uint16_t stateColor;
    if (telem.motor.stalled) {
        state = "STALL"; stateColor = FAULT_COLOR;
    } else if (telem.motor.hiZ) {
        state = "HiZ  "; stateColor = 0xFBE0;  // Yellow
    } else if (telem.motor.busy) {
        state = "RUN  "; stateColor = OK_COLOR;
    } else {
        state = "IDLE "; stateColor = OK_COLOR;
    }
    lcd.drawString(180, y, state, stateColor, BG_COLOR, TEXT_SCALE);
}

void MotionScreen::renderField(LCD& lcd, uint8_t field, uint16_t y,
                                bool selected, bool faulted, bool hiZ)
{
    bool enabled = isFieldEnabled(field, faulted, hiZ);
    uint16_t fg = enabled ? VALUE_COLOR : DISABLED_COLOR;
    uint16_t bg = selected ? SEL_BG : BG_COLOR;
    uint16_t itemW = LCD::WIDTH - 2 * MARGIN;
    char buf[32];

    lcd.fillRect(MARGIN, y, itemW, FIELD_H - 2, bg);

    switch (field) {
        case FIELD_JOG_CW:
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Jog CW", fg, bg, TEXT_SCALE);
            snprintf(buf, sizeof(buf), "[+%ld]", static_cast<long>(JOG_SIZES[m_jogSizeIdx]));
            lcd.drawString(140, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;

        case FIELD_JOG_CCW:
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Jog CCW", fg, bg, TEXT_SCALE);
            snprintf(buf, sizeof(buf), "[-%ld]", static_cast<long>(JOG_SIZES[m_jogSizeIdx]));
            lcd.drawString(140, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;

        case FIELD_JOG_SIZE:
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Jog:", fg, bg, TEXT_SCALE);
            snprintf(buf, sizeof(buf), "< %ld >", static_cast<long>(JOG_SIZES[m_jogSizeIdx]));
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;

        case FIELD_SPEED:
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Speed:", fg, bg, TEXT_SCALE);
            snprintf(buf, sizeof(buf), "< %lu >", static_cast<unsigned long>(m_speed));
            lcd.drawString(VALUE_X, y + FIELD_PAD, buf, fg, bg, TEXT_SCALE);
            break;

        case FIELD_STOP:
            lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "STOP", fg, bg, TEXT_SCALE);
            break;

        case FIELD_ENABLE: {
            Comms::TelemetrySnapshot t = Comms::g_telemetry.getSnapshot();
            if (t.motor.hiZ) {
                lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Enable Motor", fg, bg, TEXT_SCALE);
            } else {
                lcd.drawString(MARGIN + FIELD_PAD, y + FIELD_PAD, "Disable Motor", fg, bg, TEXT_SCALE);
            }
            break;
        }
    }
}

void MotionScreen::render(LCD& lcd)
{
    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();
    bool faulted = telem.motor.stalled;
    bool hiZ = telem.motor.hiZ;

    renderStatus(lcd);

    uint16_t y = FIELDS_START_Y;
    for (uint8_t i = 0; i < NUM_FIELDS; i++) {
        renderField(lcd, i, y, (i == m_selectedField), faulted, hiZ);
        y += FIELD_H;
    }
}

InputResult MotionScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();
    bool faulted = telem.motor.stalled;
    bool hiZ = telem.motor.hiZ;

    switch (dir) {
        case JoyDirection::UP:
            if (m_selectedField > 0) {
                m_selectedField--;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_selectedField < NUM_FIELDS - 1) {
                m_selectedField++;
            }
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            if (m_selectedField == FIELD_JOG_SIZE) {
                if (m_jogSizeIdx > 0) m_jogSizeIdx--;
                return InputResult::HANDLED;
            }
            if (m_selectedField == FIELD_SPEED) {
                if (m_speed > SPEED_MIN + SPEED_STEP) m_speed -= SPEED_STEP;
                else m_speed = SPEED_MIN;
                return InputResult::HANDLED;
            }
            return InputResult::EXIT_SCREEN;

        case JoyDirection::RIGHT:
            if (m_selectedField == FIELD_JOG_SIZE) {
                if (m_jogSizeIdx < NUM_JOG_SIZES - 1) m_jogSizeIdx++;
                return InputResult::HANDLED;
            }
            if (m_selectedField == FIELD_SPEED) {
                if (m_speed + SPEED_STEP <= SPEED_MAX) m_speed += SPEED_STEP;
                else m_speed = SPEED_MAX;
                return InputResult::HANDLED;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (!isFieldEnabled(m_selectedField, faulted, hiZ)) {
                return InputResult::HANDLED;
            }

            switch (m_selectedField) {
                case FIELD_JOG_CW:
                    Services::Motion::move(JOG_SIZES[m_jogSizeIdx]);
                    break;
                case FIELD_JOG_CCW:
                    Services::Motion::move(-JOG_SIZES[m_jogSizeIdx]);
                    break;
                case FIELD_SPEED:
                    Services::Motion::run(m_speed, true);
                    break;
                case FIELD_STOP:
                    Services::Motion::stop(false);
                    break;
                case FIELD_ENABLE:
                    if (telem.motor.hiZ) {
                        Services::Motion::enable();
                    } else {
                        Services::Motion::disable();
                    }
                    break;
                default:
                    break;
            }
            return InputResult::HANDLED;

        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
