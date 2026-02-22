/**
 * @file spi_hardware.hpp
 * @brief Hardware SPI driver using STM32 SPI peripheral
 *
 * Drop-in replacement for SPIBitBang that uses the actual SPI1/SPI2
 * hardware peripheral instead of GPIO bit-banging.
 *
 * Supports mode switching (Mode 0 for LCD, Mode 3 for powerSTEP01)
 * on the shared SPI1 bus.
 *
 * Locking is provided by an externally injected ILock.
 * No RTOS headers appear in this file.
 */

#ifndef SPI_HARDWARE_HPP
#define SPI_HARDWARE_HPP

#include <stdint.h>
#include <stddef.h>
#include "stm32f401xe.h"
#include "drivers/ispi_bus.hpp"
#include "platform/ilock.hpp"

/**
 * @brief Hardware SPI implementation using STM32 SPI peripheral
 *
 * Implements ISPIBus interface using the hardware SPI engine.
 * Much faster than bit-banging with precise timing guaranteed by hardware.
 */
class SPIHardware : public ISPIBus {
public:
    /**
     * @brief Construct hardware SPI on specified peripheral and pins
     *
     * @param spi       SPI peripheral (SPI1 or SPI2)
     * @param gpioPort  GPIO port for SCK/MISO/MOSI (all must be on same port)
     * @param sckPin    Pin number for SCK (0-15)
     * @param misoPin   Pin number for MISO (0-15)
     * @param mosiPin   Pin number for MOSI (0-15)
     * @param af        Alternate function number (5 for SPI1/SPI2 on F401)
     * @param mode      SPI mode (default Mode3 for powerSTEP01)
     * @param prescaler BR[2:0] prescaler value (0=/2 .. 7=/256, default 5=/64)
     * @param lock      External lock for bus arbitration
     */
    SPIHardware(SPI_TypeDef* spi,
                GPIO_TypeDef* gpioPort, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin,
                uint8_t af,
                SPIMode mode,
                uint8_t prescaler,
                ILock& lock)
        : m_spi(spi), m_mode(mode), m_lock(lock)
    {
        initGPIO(gpioPort, sckPin, misoPin, mosiPin, af);
        initSPI(prescaler);
    }

    /**
     * @brief Acquire bus lock (blocks until available)
     */
    void lock() override {
        m_lock.acquire();
    }

    /**
     * @brief Release bus lock
     */
    void unlock() override {
        m_lock.release();
    }

    /**
     * @brief Switch SPI mode (CPOL/CPHA)
     *
     * Must disable SPI to change clock polarity/phase, then re-enable.
     */
    void setMode(SPIMode mode) override {
        if (mode == m_mode) return;

        // Disable SPI to modify CPOL/CPHA
        m_spi->CR1 &= ~SPI_CR1_SPE;

        // Clear CPOL and CPHA bits
        m_spi->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);

        // Set new mode
        uint8_t mval = static_cast<uint8_t>(mode);
        if (mval & 2) m_spi->CR1 |= SPI_CR1_CPOL;  // CPOL = bit 1
        if (mval & 1) m_spi->CR1 |= SPI_CR1_CPHA;  // CPHA = bit 0

        // Re-enable SPI
        m_spi->CR1 |= SPI_CR1_SPE;
        m_mode = mode;
    }

    /**
     * @brief Get current SPI mode
     */
    SPIMode getMode() const override {
        return m_mode;
    }

    /**
     * @brief Transfer one byte (full duplex)
     *
     * Waits for TX empty, writes byte, waits for RX not empty, reads byte.
     */
    uint8_t transfer(uint8_t data) override {
        // Wait for TX buffer empty
        while (!(m_spi->SR & SPI_SR_TXE));

        // Write data to DR (starts clock + shift)
        *reinterpret_cast<volatile uint8_t*>(&m_spi->DR) = data;

        // Wait for RX buffer not empty (transfer complete)
        while (!(m_spi->SR & SPI_SR_RXNE));

        // Read received byte
        return *reinterpret_cast<volatile uint8_t*>(&m_spi->DR);
    }

    /**
     * @brief Transfer multiple bytes
     */
    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t tx = txBuf ? txBuf[i] : 0xFF;
            uint8_t rx = transfer(tx);
            if (rxBuf) {
                rxBuf[i] = rx;
            }
        }
    }

private:
    SPI_TypeDef* m_spi;
    SPIMode m_mode;
    ILock& m_lock;

    /**
     * @brief Configure GPIO pins for SPI alternate function
     */
    void initGPIO(GPIO_TypeDef* port, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin, uint8_t af) {
        // Enable GPIO clock (A, B, C)
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // Configure SCK, MOSI as AF push-pull, high speed
        configureAF(port, sckPin, af);
        configureAF(port, mosiPin, af);

        // Configure MISO as AF (input is automatic in AF mode)
        configureAF(port, misoPin, af);
    }

    /**
     * @brief Configure a single pin for alternate function
     */
    void configureAF(GPIO_TypeDef* port, uint8_t pin, uint8_t af) {
        // MODER: Alternate function (10)
        port->MODER &= ~(0x3UL << (pin * 2));
        port->MODER |= (0x2UL << (pin * 2));

        // OSPEEDR: Very high speed (11)
        port->OSPEEDR |= (0x3UL << (pin * 2));

        // OTYPER: Push-pull (0)
        port->OTYPER &= ~(1UL << pin);

        // Set alternate function in AFR[0] (pins 0-7) or AFR[1] (pins 8-15)
        uint8_t afr_idx = pin / 8;
        uint8_t afr_pos = (pin % 8) * 4;
        port->AFR[afr_idx] &= ~(0xFUL << afr_pos);
        port->AFR[afr_idx] |= (static_cast<uint32_t>(af) << afr_pos);
    }

    /**
     * @brief Initialize SPI peripheral
     */
    void initSPI(uint8_t prescaler) {
        // Enable SPI clock
        if (m_spi == SPI1) {
            RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
        } else if (m_spi == SPI2) {
            RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
        } else if (m_spi == SPI3) {
            RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
        }

        // Reset and configure SPI
        m_spi->CR1 = 0;
        m_spi->CR2 = 0;

        uint32_t cr1 = SPI_CR1_MSTR      // Master mode
                     | SPI_CR1_SSM        // Software slave management
                     | SPI_CR1_SSI        // Internal slave select high (prevents MODF)
                     | ((prescaler & 0x7) << 3);  // BR[2:0] prescaler

        // Set initial CPOL/CPHA from mode
        uint8_t mval = static_cast<uint8_t>(m_mode);
        if (mval & 2) cr1 |= SPI_CR1_CPOL;
        if (mval & 1) cr1 |= SPI_CR1_CPHA;

        m_spi->CR1 = cr1;

        // Enable SPI
        m_spi->CR1 |= SPI_CR1_SPE;

        // Dummy read to clear any pending data
        (void)m_spi->DR;
        (void)m_spi->SR;
    }
};

#endif // SPI_HARDWARE_HPP
