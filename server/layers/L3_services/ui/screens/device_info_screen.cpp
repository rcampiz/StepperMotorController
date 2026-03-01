/**
 * @file device_info_screen.cpp
 * @brief Device info / persistent memory viewer screen implementation
 */

#include "ui/screens/device_info_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/config/device_config.hpp"
#include "F_platform/interfaces/telemetry.hpp"
#include <stdio.h>

namespace UI {

// Layout (scale=3 medium font: 8px wide, 12px tall)
static constexpr uint8_t TEXT_SCALE = 3;
static constexpr uint16_t MARGIN = 6;
static constexpr uint16_t LINE_H = 15;
static constexpr uint16_t LABEL_COLOR = 0x07FF;  // Cyan
static constexpr uint16_t VALUE_COLOR = 0xFFFF;   // White
static constexpr uint16_t BG_COLOR = 0x0000;      // Black
static constexpr uint16_t VALUE_X = 72;           // After 8-char labels at 8px

void DeviceInfoScreen::onActivate()
{
    m_needsRedraw = true;
}

void DeviceInfoScreen::render(LCD& lcd)
{
    uint16_t y = MARGIN;
    char buf[32];

    // Title
    lcd.drawString(MARGIN, y, "Device Info", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H + 4;
    lcd.drawHLine(MARGIN, y, LCD::WIDTH - 2 * MARGIN, LABEL_COLOR);
    y += 6;

    // Device ID
    lcd.drawString(MARGIN, y, "ID:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    snprintf(buf, sizeof(buf), "%u", Services::g_deviceConfig.getDeviceId());
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Wheel Role
    lcd.drawString(MARGIN, y, "Role:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    lcd.drawString(VALUE_X, y, Services::roleToString(Services::g_deviceConfig.getRole()),
                   VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Default Mode
    lcd.drawString(MARGIN, y, "Mode:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    const char* mode = Services::g_deviceConfig.getDefaultMode() == 0 ? "OPEN_LOOP" : "CLOSED";
    lcd.drawString(VALUE_X, y, mode, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Separator
    y += 4;
    lcd.drawHLine(MARGIN, y, LCD::WIDTH - 2 * MARGIN, 0x8410);
    y += 6;

    // System info from telemetry
    Comms::TelemetrySnapshot telem = Comms::g_telemetry.getSnapshot();

    // Uptime
    lcd.drawString(MARGIN, y, "Uptime:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    uint32_t secs = telem.system.uptimeTicks / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    uint32_t hrs = mins / 60;
    mins %= 60;
    snprintf(buf, sizeof(buf), "%luh%lum%lus",
             static_cast<unsigned long>(hrs),
             static_cast<unsigned long>(mins),
             static_cast<unsigned long>(secs));
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Free Heap
    lcd.drawString(MARGIN, y, "Heap:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    snprintf(buf, sizeof(buf), "%lu B", static_cast<unsigned long>(telem.system.freeHeap));
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // CPU Load
    lcd.drawString(MARGIN, y, "CPU:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    snprintf(buf, sizeof(buf), "%u%%", telem.system.cpuLoad);
    lcd.drawString(VALUE_X, y, buf, VALUE_COLOR, BG_COLOR, TEXT_SCALE);
    y += LINE_H;

    // Config validity
    y += 6;
    lcd.drawString(MARGIN, y, "Config:", LABEL_COLOR, BG_COLOR, TEXT_SCALE);
    if (Services::g_deviceConfig.isValid()) {
        lcd.drawString(VALUE_X, y, "Valid", 0x07E0, BG_COLOR, TEXT_SCALE);  // Green
    } else {
        lcd.drawString(VALUE_X, y, "Defaults", 0xFBE0, BG_COLOR, TEXT_SCALE);  // Yellow
    }
}

InputResult DeviceInfoScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    if (dir == JoyDirection::LEFT) {
        return InputResult::EXIT_SCREEN;
    }

    return InputResult::HANDLED;
}

} // namespace UI
