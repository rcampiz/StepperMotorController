/**
 * @file lcd.hpp
 * @brief SPI LCD driver for X-NUCLEO-GFX01M2 (ST7789-based display)
 */

#ifndef LCD_HPP
#define LCD_HPP

#include "stm32f401xe.h"
#include "board/board_pins.hpp"
#include "drivers/spi_bus.hpp"

class LCD {
public:
    static constexpr uint16_t WIDTH = 240;
    static constexpr uint16_t HEIGHT = 240;

    // Common colors (RGB565)
    static constexpr uint16_t BLACK   = 0x0000;
    static constexpr uint16_t WHITE   = 0xFFFF;
    static constexpr uint16_t RED     = 0xF800;
    static constexpr uint16_t GREEN   = 0x07E0;
    static constexpr uint16_t BLUE    = 0x001F;
    static constexpr uint16_t YELLOW  = 0xFFE0;
    static constexpr uint16_t CYAN    = 0x07FF;
    static constexpr uint16_t MAGENTA = 0xF81F;

    LCD(SPIBus& spi) : m_spi(spi) {
        initPins();
    }

    void init() {
        reset();

        writeCmd(0x11);  // Sleep out
        delayMs(120);

        writeCmd(0x36);  // MADCTL
        writeData(0x00);

        writeCmd(0x3A);  // Color mode
        writeData(0x55); // 16-bit RGB565

        writeCmd(0x21);  // Inversion on (for correct colors)

        writeCmd(0x29);  // Display on
        delayMs(20);
    }

    void reset() {
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

        writeCmd(0x2B);  // Row address set
        writeData(y0 >> 8);
        writeData(y0 & 0xFF);
        writeData(y1 >> 8);
        writeData(y1 & 0xFF);

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

private:
    SPIBus& m_spi;

    void initPins() {
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN
                     | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOHEN;

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
        csLow();
        dcLow();
        m_spi.transfer(cmd);
        csHigh();
        m_spi.unlock();
    }

    void writeData(uint8_t data) {
        m_spi.lock();
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
