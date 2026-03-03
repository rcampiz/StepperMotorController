/**
 * @file idisplay_dispatcher.hpp
 * @brief Interface for display remote rendering (UI:DISP namespace)
 */

#pragma once

#include "F_platform/types/dispatch_result.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Comms {

class IDisplayDispatcher {
public:
    virtual ~IDisplayDispatcher() = default;

    // UI mode
    virtual bool displayIsRemoteMode() = 0;
    virtual uint8_t displayGetMode() = 0;
    virtual ServiceStatus displaySetMode(uint8_t mode) = 0;

    // One-shot rendering
    virtual void displayClear(uint16_t color) = 0;
    virtual void displayText(uint16_t x, uint16_t y, const char* text,
                              uint16_t fg, uint16_t bg) = 0;
    virtual void displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              uint16_t color, bool filled) = 0;
    virtual void displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                              uint16_t color) = 0;
    virtual void displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                const uint8_t* data, uint32_t len) = 0;
    virtual void displayIndicator(uint16_t angle, int8_t rotation, bool translation) = 0;

    // Bitmap streaming
    virtual bool displayStreamStart(uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h) = 0;
    virtual void displayStreamData(const uint8_t* data, uint32_t len) = 0;
    virtual void displayStreamEnd() = 0;
};

} // namespace Comms
