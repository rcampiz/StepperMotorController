/**
 * @file lcd_st7789.hpp
 * @brief SPI LCD driver for X-NUCLEO-GFX01M2 (ST7789-based display)
 */

#ifndef LCD_ST7789_HPP
#define LCD_ST7789_HPP

#include <stdint.h>
#include "stm32f401xe.h"
#include "board/board_pins.hpp"
#include "drivers/spi_bus.hpp"

// 5x7 bitmap font (ASCII 32-126)
// Each character is 5 columns, each column is 7 bits (LSB = top)
static const uint8_t FONT_5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
    {0x00, 0x08, 0x14, 0x22, 0x41}, // 60 '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
    {0x41, 0x22, 0x14, 0x08, 0x00}, // 62 '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // 70 'F'
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // 71 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // 77 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // 87 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
    {0x03, 0x04, 0x78, 0x04, 0x03}, // 89 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 'Z'
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // 91 '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\'
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // 93 ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96 '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102 'f'
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // 103 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106 'j'
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // 107 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 123 '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // 124 '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 '}'
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // 126 '~'
};

class LCD {
public:
    static constexpr uint16_t WIDTH = 240;
    static constexpr uint16_t HEIGHT = 240;

    // Row offset: 240x240 panel maps to rows 80-319 of the 320-row ST7789 RAM
    static constexpr uint16_t ROW_OFFSET = 80;

    // Font dimensions
    static constexpr uint8_t CHAR_WIDTH = 6;   // 5 pixels + 1 spacing
    static constexpr uint8_t CHAR_HEIGHT = 8;  // 7 pixels + 1 spacing

    // Common colors (RGB565)
    static constexpr uint16_t BLACK   = 0x0000;
    static constexpr uint16_t WHITE   = 0xFFFF;
    static constexpr uint16_t RED     = 0xF800;
    static constexpr uint16_t GREEN   = 0x07E0;
    static constexpr uint16_t BLUE    = 0x001F;
    static constexpr uint16_t YELLOW  = 0xFFE0;
    static constexpr uint16_t CYAN    = 0x07FF;
    static constexpr uint16_t MAGENTA = 0xF81F;
    static constexpr uint16_t GRAY    = 0x8410;

    LCD(SPIBus& spi) : m_spi(spi) {
        initPins();
    }

    void init() {
        // Try hardware reset first
        reset();

        // Software reset as backup
        writeCmd(0x01);  // Software reset
        delayMs(150);

        writeCmd(0x11);  // Sleep out
        delayMs(120);

        writeCmd(0x36);  // MADCTL
        writeData(0x08); // BGR subpixel order for GFX01M2 panel

        writeCmd(0x3A);  // Color mode
        writeData(0x55); // 16-bit RGB565

        writeCmd(0x21);  // Inversion on (for correct colors)

        writeCmd(0x29);  // Display on
        delayMs(20);
    }

    void reset() {
        // Hardware reset - may not work if GPIOH has issues
        nresetHigh();
        delayMs(10);
        nresetLow();
        delayMs(10);
        nresetHigh();
        delayMs(120);
    }

    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        writeCmd(0x2A);  // Column address set
        writeData(x0 >> 8);
        writeData(x0 & 0xFF);
        writeData(x1 >> 8);
        writeData(x1 & 0xFF);

        // Apply row offset for 240x240 panel in 240x320 controller RAM
        uint16_t r0 = y0 + ROW_OFFSET;
        uint16_t r1 = y1 + ROW_OFFSET;
        writeCmd(0x2B);  // Row address set
        writeData(r0 >> 8);
        writeData(r0 & 0xFF);
        writeData(r1 >> 8);
        writeData(r1 & 0xFF);

        writeCmd(0x2C);  // Memory write
    }

    void fillScreen(uint16_t color) {
        fillRect(0, 0, WIDTH, HEIGHT, color);
    }

    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
        setWindow(x, y, x + w - 1, y + h - 1);

        uint8_t hi = color >> 8;
        uint8_t lo = color & 0xFF;

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();  // Data mode

        for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
            m_spi.transfer(hi);
            m_spi.transfer(lo);
        }

        csHigh();
        m_spi.unlock();
    }

    void drawPixel(uint16_t x, uint16_t y, uint16_t color) {
        if (x >= WIDTH || y >= HEIGHT) return;
        setWindow(x, y, x, y);

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();
        m_spi.transfer(color >> 8);
        m_spi.transfer(color & 0xFF);
        csHigh();
        m_spi.unlock();
    }

    void drawTestPattern() {
        // Draw color bars
        uint16_t barWidth = WIDTH / 8;
        uint16_t colors[] = {WHITE, YELLOW, CYAN, GREEN, MAGENTA, RED, BLUE, BLACK};

        for (int i = 0; i < 8; i++) {
            fillRect(i * barWidth, 0, barWidth, HEIGHT, colors[i]);
        }
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

    /**
     * @brief Draw a single character
     * @param x X position (top-left)
     * @param y Y position (top-left)
     * @param c Character to draw (ASCII 32-126)
     * @param fg Foreground color
     * @param bg Background color
     */
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg) {
        if (c < 32 || c > 126) c = '?';
        const uint8_t* glyph = FONT_5X7[c - 32];

        for (uint8_t col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (uint8_t row = 0; row < 7; row++) {
                uint16_t color = (line & (1 << row)) ? fg : bg;
                drawPixel(x + col, y + row, color);
            }
        }
        // Draw spacing column
        for (uint8_t row = 0; row < 7; row++) {
            drawPixel(x + 5, y + row, bg);
        }
    }

    /**
     * @brief Draw a string
     * @param x X position (top-left of first character)
     * @param y Y position (top-left)
     * @param str Null-terminated string
     * @param fg Foreground color
     * @param bg Background color
     */
    void drawString(uint16_t x, uint16_t y, const char* str, uint16_t fg, uint16_t bg) {
        while (*str) {
            if (x + CHAR_WIDTH > WIDTH) {
                x = 0;
                y += CHAR_HEIGHT;
                if (y + CHAR_HEIGHT > HEIGHT) break;
            }
            drawChar(x, y, *str++, fg, bg);
            x += CHAR_WIDTH;
        }
    }

    /**
     * @brief Draw an integer value (right-aligned in field)
     * @param x X position (right edge of field)
     * @param y Y position (top-left)
     * @param value Integer to display
     * @param fieldWidth Minimum field width (for right alignment)
     * @param fg Foreground color
     * @param bg Background color
     */
    void drawInt(uint16_t x, uint16_t y, int32_t value, uint8_t fieldWidth, uint16_t fg, uint16_t bg) {
        char buf[12];
        int idx = 0;
        bool neg = false;

        if (value < 0) {
            neg = true;
            value = -value;
        }

        // Convert to string (reversed)
        if (value == 0) {
            buf[idx++] = '0';
        } else {
            while (value > 0) {
                buf[idx++] = '0' + (value % 10);
                value /= 10;
            }
        }
        if (neg) buf[idx++] = '-';

        // Pad with spaces
        while (idx < fieldWidth) {
            buf[idx++] = ' ';
        }

        // Draw reversed
        uint16_t px = x - CHAR_WIDTH;
        for (int i = 0; i < idx; i++) {
            drawChar(px, y, buf[i], fg, bg);
            px -= CHAR_WIDTH;
        }
    }

    /**
     * @brief Draw an unsigned integer value (right-aligned in field)
     */
    void drawUInt(uint16_t x, uint16_t y, uint32_t value, uint8_t fieldWidth, uint16_t fg, uint16_t bg) {
        char buf[12];
        int idx = 0;

        if (value == 0) {
            buf[idx++] = '0';
        } else {
            while (value > 0) {
                buf[idx++] = '0' + (value % 10);
                value /= 10;
            }
        }

        while (idx < fieldWidth) {
            buf[idx++] = ' ';
        }

        uint16_t px = x - CHAR_WIDTH;
        for (int i = 0; i < idx; i++) {
            drawChar(px, y, buf[i], fg, bg);
            px -= CHAR_WIDTH;
        }
    }

    /**
     * @brief Draw a line using Bresenham's algorithm
     * @param x0, y0 Start point
     * @param x1, y1 End point
     * @param color Line color
     */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        int16_t dx = static_cast<int16_t>(x1 > x0 ? x1 - x0 : x0 - x1);
        int16_t dy = static_cast<int16_t>(y1 > y0 ? y1 - y0 : y0 - y1);
        int16_t sx = static_cast<int16_t>(x0 < x1 ? 1 : -1);
        int16_t sy = static_cast<int16_t>(y0 < y1 ? 1 : -1);
        int16_t err = static_cast<int16_t>(dx - dy);

        while (true) {
            if (x0 >= 0 && x0 < WIDTH && y0 >= 0 && y0 < HEIGHT) {
                drawPixel(x0, y0, color);
            }
            if (x0 == x1 && y0 == y1) {
                break;
            }
            int16_t e2 = static_cast<int16_t>(2 * err);
            if (e2 > -dy) {
                err = static_cast<int16_t>(err - dy);
                x0 = static_cast<int16_t>(x0 + sx);
            }
            if (e2 < dx) {
                err = static_cast<int16_t>(err + dx);
                y0 = static_cast<int16_t>(y0 + sy);
            }
        }
    }

    /**
     * @brief Draw a bitmap from RGB565 pixel data
     * @param x, y Top-left position
     * @param w, h Bitmap dimensions
     * @param data Pointer to RGB565 pixel data (big-endian)
     */
    void drawBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data) {
        if (x >= WIDTH || y >= HEIGHT) {
            return;
        }

        // Clip to screen bounds
        uint16_t drawW = (x + w > WIDTH) ? WIDTH - x : w;
        uint16_t drawH = (y + h > HEIGHT) ? HEIGHT - y : h;

        setWindow(x, y, x + drawW - 1, y + drawH - 1);

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();

        for (uint16_t row = 0; row < drawH; row++) {
            for (uint16_t col = 0; col < drawW; col++) {
                uint16_t pixel = data[(row * w) + col];
                m_spi.transfer(pixel >> 8);
                m_spi.transfer(pixel & 0xFF);
            }
        }

        csHigh();
        m_spi.unlock();
    }

    /**
     * @brief Draw a bitmap from raw byte data (RGB565 big-endian)
     * @param x, y Top-left position
     * @param w, h Bitmap dimensions
     * @param data Pointer to raw byte data (2 bytes per pixel, big-endian)
     * @param len Length of data in bytes
     */
    void drawBitmapRaw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint8_t* data, size_t len) {
        if (x >= WIDTH || y >= HEIGHT) {
            return;
        }

        uint16_t drawW = (x + w > WIDTH) ? WIDTH - x : w;
        uint16_t drawH = (y + h > HEIGHT) ? HEIGHT - y : h;

        setWindow(x, y, x + drawW - 1, y + drawH - 1);

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();

        // Transfer raw bytes directly
        size_t maxBytes = static_cast<size_t>(drawW) * drawH * 2;
        size_t transferLen = len < maxBytes ? len : maxBytes;
        for (size_t i = 0; i < transferLen; i++) {
            m_spi.transfer(data[i]);
        }

        csHigh();
        m_spi.unlock();
    }

    /**
     * @brief Start streaming bitmap data (for chunked transfers)
     *
     * Call this before streamBitmapData(). Must call streamBitmapEnd() when done.
     * Holds the SPI lock until streamBitmapEnd() is called.
     *
     * @param x, y Top-left position
     * @param w, h Bitmap dimensions
     */
    void streamBitmapStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
        if (x >= WIDTH || y >= HEIGHT) {
            return;
        }

        uint16_t drawW = (x + w > WIDTH) ? WIDTH - x : w;
        uint16_t drawH = (y + h > HEIGHT) ? HEIGHT - y : h;

        setWindow(x, y, x + drawW - 1, y + drawH - 1);

        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();
        m_streaming = true;
    }

    /**
     * @brief Stream bitmap data chunk
     *
     * Must be called between streamBitmapStart() and streamBitmapEnd().
     *
     * @param data Pointer to raw RGB565 byte data
     * @param len Number of bytes to transfer
     */
    void streamBitmapData(const uint8_t* data, size_t len) {
        if (!m_streaming) {
            return;
        }
        for (size_t i = 0; i < len; i++) {
            m_spi.transfer(data[i]);
        }
    }

    /**
     * @brief End bitmap streaming
     *
     * Releases the SPI lock held by streamBitmapStart().
     */
    void streamBitmapEnd() {
        if (!m_streaming) {
            return;
        }
        csHigh();
        m_spi.unlock();
        m_streaming = false;
    }

    /**
     * @brief Check if currently streaming bitmap data
     * @return true if streaming is active
     */
    bool isStreaming() const { return m_streaming; }

private:
    bool m_streaming = false;
    SPIBus& m_spi;

    void initPins() {
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN
                     | RCC_AHB1ENR_GPIOCEN;

        // CS - output
        configureOutput(Pins::GFX_LCD::CS_PORT, Pins::GFX_LCD::CS_PIN);
        csHigh();

        // DC - output
        configureOutput(Pins::GFX_LCD::DC_PORT, Pins::GFX_LCD::DC_PIN);
        dcLow();

        // NRESET - output
        configureOutput(Pins::GFX_LCD::NRESET_PORT, Pins::GFX_LCD::NRESET_PIN);
        nresetHigh();

        // TE - input (optional tearing effect sync)
        Pins::GFX_LCD::TE_PORT->MODER &= ~(0x3 << (Pins::GFX_LCD::TE_PIN * 2));
    }

    void configureOutput(GPIO_TypeDef* port, uint8_t pin) {
        port->MODER &= ~(0x3 << (pin * 2));
        port->MODER |= (0x1 << (pin * 2));
        port->OTYPER &= ~(1 << pin);
        port->OSPEEDR |= (0x3 << (pin * 2));
    }

    void csLow()      { Pins::GFX_LCD::CS_PORT->BSRR = (1 << (Pins::GFX_LCD::CS_PIN + 16)); }
    void csHigh()     { Pins::GFX_LCD::CS_PORT->BSRR = (1 << Pins::GFX_LCD::CS_PIN); }
    void dcLow()      { Pins::GFX_LCD::DC_PORT->BSRR = (1 << (Pins::GFX_LCD::DC_PIN + 16)); }
    void dcHigh()     { Pins::GFX_LCD::DC_PORT->BSRR = (1 << Pins::GFX_LCD::DC_PIN); }
    void nresetLow()  { Pins::GFX_LCD::NRESET_PORT->BSRR = (1 << (Pins::GFX_LCD::NRESET_PIN + 16)); }
    void nresetHigh() { Pins::GFX_LCD::NRESET_PORT->BSRR = (1 << Pins::GFX_LCD::NRESET_PIN); }

    void writeCmd(uint8_t cmd) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcLow();
        m_spi.transfer(cmd);
        csHigh();
        m_spi.unlock();
    }

    void writeData(uint8_t data) {
        m_spi.lock();
        m_spi.setMode(SPIBus::Mode::Mode0);
        csLow();
        dcHigh();
        m_spi.transfer(data);
        csHigh();
        m_spi.unlock();
    }

    void delayMs(uint32_t ms) {
        for (volatile uint32_t i = 0; i < ms * 8000; i++);
    }
};

#endif // LCD_HPP
