/**
 * @file display_dispatch.cpp
 * @brief L3 IDisplayDispatcher implementation
 */

#include "display_dispatch.hpp"
#include "harness/pins/iui_mode.hpp"

namespace Services {

bool DisplayDispatch::displayIsRemoteMode() {
    return Harness::uiMode().getMode() == UI::UIMode::REMOTE;
}

uint8_t DisplayDispatch::displayGetMode() {
    return static_cast<uint8_t>(Harness::uiMode().getMode());
}

Harness::ServiceStatus DisplayDispatch::displaySetMode(uint8_t mode) {
    if (mode > static_cast<uint8_t>(UI::UIMode::REMOTE)) {
        return Harness::ServiceStatus::InvalidParam;
    }
    Harness::uiMode().setMode(static_cast<UI::UIMode>(mode));
    return Harness::ServiceStatus::Ok;
}

void DisplayDispatch::displayClear(uint16_t color) {
    m_remoteDisplay->clear(color);
}

void DisplayDispatch::displayText(uint16_t x, uint16_t y, const char* text,
                                   uint16_t fg, uint16_t bg) {
    m_remoteDisplay->text(x, y, text, fg, bg);
}

void DisplayDispatch::displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   uint16_t color, bool filled) {
    m_remoteDisplay->rect(x, y, w, h, color, filled);
}

void DisplayDispatch::displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   uint16_t color) {
    m_remoteDisplay->line(x0, y0, x1, y1, color);
}

void DisplayDispatch::displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     const uint8_t* data, uint32_t len) {
    m_remoteDisplay->bitmap(x, y, w, h, data, len);
}

void DisplayDispatch::displayIndicator(uint16_t angle, int8_t rotation, bool translation) {
    m_remoteDisplay->indicator(angle, rotation, translation);
}

bool DisplayDispatch::displayStreamStart(uint16_t x, uint16_t y,
                                          uint16_t w, uint16_t h) {
    return m_remoteDisplay->streamStart(x, y, w, h);
}

void DisplayDispatch::displayStreamData(const uint8_t* data, uint32_t len) {
    m_remoteDisplay->streamData(data, len);
}

void DisplayDispatch::displayStreamEnd() {
    m_remoteDisplay->streamEnd();
}

} // namespace Services
