/**
 * @file iremote_display.hpp
 * @brief Interface for remote LCD rendering operations
 *
 * Signal net from L3 (dispatcher) down to F_platform (display task).
 * Implemented directly in display_task.cpp.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Harness {

class IRemoteDisplay {
public:
    virtual ~IRemoteDisplay() = default;

    virtual void clear(uint16_t color) = 0;
    virtual void text(uint16_t x, uint16_t y, const char* text,
                       uint16_t fg, uint16_t bg) = 0;
    virtual void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t color, bool filled) = 0;
    virtual void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       uint16_t color) = 0;
    virtual void bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const uint8_t* data, uint32_t len) = 0;
    virtual void indicator(uint16_t angle, int8_t rotation, bool translation) = 0;

    virtual bool streamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    virtual void streamData(const uint8_t* data, uint32_t len) = 0;
    virtual void streamEnd() = 0;
};

} // namespace Harness
