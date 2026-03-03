/**
 * @file sd_display.cpp
 * @brief ServiceDispatcher — IDisplayDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "F_platform/ui/ui_mode.hpp"

bool ServiceDispatcher::displayIsRemoteMode() {
    return UI::g_uiMode.getMode() == UI::UIMode::REMOTE;
}

uint8_t ServiceDispatcher::displayGetMode() {
    return static_cast<uint8_t>(UI::g_uiMode.getMode());
}

Comms::ServiceStatus ServiceDispatcher::displaySetMode(uint8_t mode) {
    if (mode > static_cast<uint8_t>(UI::UIMode::REMOTE)) {
        return Comms::ServiceStatus::InvalidParam;
    }
    UI::g_uiMode.setMode(static_cast<UI::UIMode>(mode));
    return Comms::ServiceStatus::Ok;
}

void ServiceDispatcher::displayClear(uint16_t color) {
    Tasks::DisplayTask_RemoteClear(color);
}

void ServiceDispatcher::displayText(uint16_t x, uint16_t y, const char* text,
                                     uint16_t fg, uint16_t bg) {
    Tasks::DisplayTask_RemoteText(x, y, text, fg, bg);
}

void ServiceDispatcher::displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     uint16_t color, bool filled) {
    Tasks::DisplayTask_RemoteRect(x, y, w, h, color, filled);
}

void ServiceDispatcher::displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                     uint16_t color) {
    Tasks::DisplayTask_RemoteLine(x0, y0, x1, y1, color);
}

void ServiceDispatcher::displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                       const uint8_t* data, uint32_t len) {
    Tasks::DisplayTask_RemoteBitmap(x, y, w, h, data, len);
}

void ServiceDispatcher::displayIndicator(uint16_t angle, int8_t rotation, bool translation) {
    Tasks::DisplayTask_RemoteIndicator(angle, rotation, translation);
}

bool ServiceDispatcher::displayStreamStart(uint16_t x, uint16_t y,
                                            uint16_t w, uint16_t h) {
    return Tasks::DisplayTask_StreamBitmapStart(x, y, w, h);
}

void ServiceDispatcher::displayStreamData(const uint8_t* data, uint32_t len) {
    Tasks::DisplayTask_StreamBitmapData(data, len);
}

void ServiceDispatcher::displayStreamEnd() {
    Tasks::DisplayTask_StreamBitmapEnd();
}
