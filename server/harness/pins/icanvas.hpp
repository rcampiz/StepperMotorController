/**
 * @file icanvas.hpp
 * @brief Display canvas interface — boundary contract
 *
 * Abstract drawing surface for L3 UI screens. Implemented by Drivers::LCD.
 * Screens render through this interface without seeing SPI/GPIO details.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Harness {

namespace Canvas {
    constexpr uint16_t WIDTH  = 240;
    constexpr uint16_t HEIGHT = 320;

    // Font dimensions
    constexpr uint8_t CHAR_WIDTH  = 6;   // 5 pixels + 1 spacing
    constexpr uint8_t CHAR_HEIGHT = 8;   // 7 pixels + 1 spacing
    constexpr uint8_t MED_CHAR_W  = 8;   // medium font (scale=3)
    constexpr uint8_t MED_CHAR_H  = 12;

    // Common colors (RGB565)
    constexpr uint16_t BLACK   = 0x0000;
    constexpr uint16_t WHITE   = 0xFFFF;
    constexpr uint16_t RED     = 0xF800;
    constexpr uint16_t GREEN   = 0x07E0;
    constexpr uint16_t BLUE    = 0x001F;
    constexpr uint16_t YELLOW  = 0xFFE0;
    constexpr uint16_t CYAN    = 0x07FF;
    constexpr uint16_t MAGENTA = 0xF81F;
    constexpr uint16_t GRAY    = 0x8410;
} // namespace Canvas

class ICanvas {
public:
    virtual ~ICanvas() = default;

    // --- Core drawing primitives (pure virtual) ---

    virtual void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) = 0;
    virtual void drawPixel(uint16_t x, uint16_t y, uint16_t color) = 0;
    virtual void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) = 0;

    // --- Text rendering ---

    virtual void drawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg,
                          uint8_t scale = 1) = 0;
    virtual void drawString(uint16_t x, uint16_t y, const char* str, uint16_t fg, uint16_t bg,
                            uint8_t scale = 1) = 0;
    virtual void drawInt(uint16_t x, uint16_t y, int32_t value, uint8_t fieldWidth,
                         uint16_t fg, uint16_t bg, uint8_t scale = 1) = 0;
    virtual void drawUInt(uint16_t x, uint16_t y, uint32_t value, uint8_t fieldWidth,
                          uint16_t fg, uint16_t bg, uint8_t scale = 1) = 0;

    // --- DMA-accelerated text ---

    virtual void blitTextLine(uint16_t x, uint16_t y, uint16_t lineW, uint16_t lineH,
                              const char* text, uint16_t fg, uint16_t bg) = 0;
    virtual void blitTextLineColored(uint16_t x, uint16_t y, uint16_t lineW, uint16_t lineH,
                                     const char* text, const uint16_t* charColors, uint16_t bg) = 0;

    // --- Bitmap / column ---

    virtual void drawBitmapRaw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               const uint8_t* data, size_t len) = 0;
    virtual void blitColumn(uint16_t x, uint16_t y, uint16_t h, const uint8_t* data) = 0;

    // --- Streaming bitmap ---

    virtual void streamBitmapStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    virtual void streamBitmapData(const uint8_t* data, size_t len) = 0;
    virtual void streamBitmapEnd() = 0;
    virtual bool isStreaming() const = 0;

    // --- Test pattern ---

    virtual void drawTestPattern() = 0;

    // --- Inline helpers (delegate to fillRect) ---

    void fillScreen(uint16_t color) {
        fillRect(0, 0, Canvas::WIDTH, Canvas::HEIGHT, color);
    }
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
        fillRect(x, y, w, 1, color);
    }
    void drawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
        fillRect(x, y, 1, h, color);
    }
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
        drawHLine(x, y, w, color);
        drawHLine(x, y + h - 1, w, color);
        drawVLine(x, y, h, color);
        drawVLine(x + w - 1, y, h, color);
    }
};

} // namespace Harness
