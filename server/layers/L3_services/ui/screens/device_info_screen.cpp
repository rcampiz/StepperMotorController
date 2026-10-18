/**
 * @file device_info_screen.cpp
 * @brief Device info: static rows once, live rows when their text changes.
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
static constexpr uint16_t ROW_UPTIME = 4, ROW_HEAP = 5, ROW_CPU = 6;   // after a 1-row gap

static uint16_t rowY(uint16_t i) { return static_cast<uint16_t>(ROWS_Y + i * Chrome::ROW_H); }

void DeviceInfoScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
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
        Chrome::fieldRow(lcd, rowY(1), "Wheel", Services::roleToString(Services::g_deviceConfig.getRole()),
                         Chrome::RowState::NORMAL, Theme::YELLOW);
        Chrome::fieldRow(lcd, rowY(2), "Mode",
                         Services::g_deviceConfig.getDefaultMode() == 0 ? "OPEN LOOP" : "CLOSED",
                         Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(3), "Config", Services::g_deviceConfig.isValid() ? "saved" : "defaults",
                         Chrome::RowState::NORMAL, Services::g_deviceConfig.isValid() ? Theme::GREEN : Theme::ORANGE);
        Chrome::fieldRow(lcd, rowY(ROW_UPTIME), "Uptime", "", Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(ROW_HEAP), "Free heap", "", Chrome::RowState::NORMAL);
        Chrome::fieldRow(lcd, rowY(ROW_CPU), "CPU load", "", Chrome::RowState::NORMAL);
        Chrome::drawDeck(lcd, static_cast<uint16_t>(rowY(7) + 6), Theme::CONTENT_BOTTOM);
    }

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

InputResult DeviceInfoScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    if (dir == JoyDirection::LEFT) return InputResult::EXIT_SCREEN;
    return InputResult::HANDLED;
}

} // namespace UI
