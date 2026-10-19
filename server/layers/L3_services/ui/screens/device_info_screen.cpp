/**
 * @file device_info_screen.cpp
 * @brief Device info: static rows once, live rows when their text changes, the wheel role
 *        editable and saved to flash.
 */

#include "ui/screens/device_info_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/config/device_config/device_config.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t ROWS_Y = Theme::CONTENT_Y + 4;
static constexpr uint16_t ROW_WHEEL = 1, ROW_UPTIME = 4, ROW_HEAP = 5, ROW_CPU = 6;
static constexpr uint16_t HINT_Y = ROWS_Y + 7 * Chrome::ROW_H + 6;
static constexpr uint16_t HELP_Y = HINT_Y + Chrome::HELP_LINE_H;     // the row help, under the key hint
static constexpr uint16_t DECK_Y = HELP_Y + Chrome::HELP_H + 4;
static_assert(DECK_Y + 12 < Theme::CONTENT_BOTTOM, "the rows, hint and help fit above the footer");
/// The one selectable row's help (const, in flash)
static constexpr const char* HELP_WHEEL = "Which wheel this board drives on the\nrobot: FL, FR, RL or RR.";
const char* DeviceInfoScreen::helpText() { return HELP_WHEEL; }
static constexpr uint8_t ROLE_COUNT = 5;          // NONE, FL, FR, RL, RR
static constexpr const char* HINT_IDLE = "CENTER: change the wheel";
static constexpr const char* HINT_EDIT = "LEFT/RIGHT choose  CENTER save";

static uint16_t rowY(uint16_t i) { return static_cast<uint16_t>(ROWS_Y + i * Chrome::ROW_H); }

static const char* roleLong(uint8_t r)
{
    switch (r) {
        case 1:  return "front left";
        case 2:  return "front right";
        case 3:  return "rear left";
        case 4:  return "rear right";
        default: return "none";
    }
}

void DeviceInfoScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_editing = false;
    m_role = static_cast<uint8_t>(Services::g_deviceConfig.getRole());
    if (m_role >= ROLE_COUNT) m_role = 0;
    snprintf(m_hint, sizeof m_hint, "%s", HINT_IDLE);
    m_uptime[0] = m_heap[0] = m_cpu[0] = '\0';
}

void DeviceInfoScreen::render(Services::ICanvas& lcd)
{
    Protocol::TelemetrySnapshot telem = Services::g_telemetryStore.getSnapshot();
    char buf[16];

    if (m_full) {
        m_full = false;
        snprintf(buf, sizeof buf, "%u", Services::g_deviceConfig.getDeviceId());
        Chrome::fieldRow(lcd, rowY(0), "Device id", buf, Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(2), "Mode",
                         Services::g_deviceConfig.getDefaultMode() == 0 ? "OPEN LOOP" : "CLOSED",
                         Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(3), "Config", Services::g_deviceConfig.isValid() ? "saved" : "defaults",
                         Chrome::RowState::NORMAL, Services::g_deviceConfig.isValid() ? Theme::GREEN : Theme::ORANGE);
        Chrome::fieldRow(lcd, rowY(ROW_UPTIME), "Uptime", "", Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(ROW_HEAP), "Free heap", "", Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(ROW_CPU), "CPU load", "", Chrome::RowState::NORMAL);
        Chrome::drawDeck(lcd, DECK_Y, Theme::CONTENT_BOTTOM);
        m_help.invalidate();
        m_wheelDirty = true;
        m_hintDirty = true;
    }
    if (m_wheelDirty) {
        m_wheelDirty = false;
        Chrome::fieldRow(lcd, rowY(ROW_WHEEL), "Wheel", roleLong(m_role),
                         m_editing ? Chrome::RowState::EDITING : Chrome::RowState::SELECTED, Theme::YELLOW);
    }
    if (m_hintDirty) {
        m_hintDirty = false;
        Chrome::hintLine(lcd, HINT_Y, m_hint, Theme::DIM);
    }
    m_help.show(lcd, HELP_Y, HELP_WHEEL);                 // the Wheel is always the selection

    uint32_t secs = telem.system.uptimeTicks / 1000;
    uint32_t mins = secs / 60; secs %= 60;
    uint32_t hrs = mins / 60;  mins %= 60;
    snprintf(buf, sizeof buf, "%luh%02lum%02lus", static_cast<unsigned long>(hrs),
             static_cast<unsigned long>(mins), static_cast<unsigned long>(secs));
    if (strcmp(buf, m_uptime) != 0) {
        snprintf(m_uptime, sizeof m_uptime, "%s", buf);
        Chrome::fieldRow(lcd, rowY(ROW_UPTIME), "Uptime", buf, Chrome::RowState::NORMAL, 0, false);
    }
    snprintf(buf, sizeof buf, "%lu B", static_cast<unsigned long>(telem.system.freeHeap));
    if (strcmp(buf, m_heap) != 0) {
        snprintf(m_heap, sizeof m_heap, "%s", buf);
        Chrome::fieldRow(lcd, rowY(ROW_HEAP), "Free heap", buf, Chrome::RowState::NORMAL, 0, false);
    }
    snprintf(buf, sizeof buf, "%u%%", telem.system.cpuLoad);
    if (strcmp(buf, m_cpu) != 0) {
        snprintf(m_cpu, sizeof m_cpu, "%s", buf);
        Chrome::fieldRow(lcd, rowY(ROW_CPU), "CPU load", buf, Chrome::RowState::NORMAL,
                         telem.system.cpuLoad > 80 ? Theme::ORANGE : 0, false);
    }
}

void DeviceInfoScreen::setHint(const char* text)
{
    snprintf(m_hint, sizeof m_hint, "%s", text);
    m_hintDirty = true;
    m_wheelDirty = true;
}

InputResult DeviceInfoScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    if (!m_editing) {
        if (dir == JoyDirection::LEFT) return InputResult::EXIT_SCREEN;
        if (dir == JoyDirection::CENTER || dir == JoyDirection::RIGHT) {
            m_editing = true;
            setHint(HINT_EDIT);
        }
        return InputResult::HANDLED;
    }
    switch (dir) {
        case JoyDirection::LEFT:
            m_role = static_cast<uint8_t>((m_role + ROLE_COUNT - 1) % ROLE_COUNT);
            m_wheelDirty = true;
            break;
        case JoyDirection::RIGHT:
            m_role = static_cast<uint8_t>((m_role + 1) % ROLE_COUNT);
            m_wheelDirty = true;
            break;
        case JoyDirection::CENTER: {
            m_editing = false;
            const bool ok = Services::g_deviceConfig.setRole(static_cast<Services::WheelRole>(m_role));
            char msg[40];
            if (ok) snprintf(msg, sizeof msg, "saved: %s", roleLong(m_role));
            else snprintf(msg, sizeof msg, "NOT saved (flash error)");
            setHint(msg);
            break;
        }
        case JoyDirection::UP:
        case JoyDirection::DOWN:                          // leave without saving
            m_editing = false;
            m_role = static_cast<uint8_t>(Services::g_deviceConfig.getRole());
            if (m_role >= ROLE_COUNT) m_role = 0;
            setHint("not changed");
            break;
        default:
            break;
    }
    return InputResult::HANDLED;
}

} // namespace UI
