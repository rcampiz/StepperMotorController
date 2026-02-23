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
     * @brief Construct hardware SPI on specified peripheral and pins (single port)
     *
     * @param spi       SPI peripheral (SPI1, SPI2, SPI3)
     * @param gpioPort  GPIO port for SCK/MISO/MOSI (all on same port)
     * @param sckPin    Pin number for SCK (0-15)
     * @param misoPin   Pin number for MISO (0-15)
     * @param mosiPin   Pin number for MOSI (0-15)
     * @param af        Alternate function number
     * @param mode      SPI mode
     * @param prescaler BR[2:0] prescaler value (0=/2 .. 7=/256)
     * @param lock      External lock for bus arbitration
     */
    SPIHardware(SPI_TypeDef* spi,
                GPIO_TypeDef* gpioPort, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin,
                uint8_t af,
                SPIMode mode,
                uint8_t prescaler,
                ILock& lock)
        : m_spi(spi), m_mode(mode), m_prescaler(prescaler), m_lock(lock)
    {
        initGPIO(gpioPort, sckPin, gpioPort, misoPin, gpioPort, mosiPin, af);
        initSPI(prescaler);
    }

    /**
     * @brief Construct hardware SPI with pins on different GPIO ports
     *
     * @param spi        SPI peripheral (SPI1, SPI2, SPI3)
     * @param sckPort    GPIO port for SCK
     * @param sckPin     Pin number for SCK (0-15)
     * @param misoPort   GPIO port for MISO
     * @param misoPin    Pin number for MISO (0-15)
     * @param mosiPort   GPIO port for MOSI
     * @param mosiPin    Pin number for MOSI (0-15)
     * @param af         Alternate function number
     * @param mode       SPI mode
     * @param prescaler  BR[2:0] prescaler value (0=/2 .. 7=/256)
     * @param lock       External lock for bus arbitration
     */
    SPIHardware(SPI_TypeDef* spi,
                GPIO_TypeDef* sckPort, uint8_t sckPin,
                GPIO_TypeDef* misoPort, uint8_t misoPin,
                GPIO_TypeDef* mosiPort, uint8_t mosiPin,
                uint8_t af,
                SPIMode mode,
                uint8_t prescaler,
                ILock& lock)
        : m_spi(spi), m_mode(mode), m_prescaler(prescaler), m_lock(lock)
    {
        initGPIO(sckPort, sckPin, misoPort, misoPin, mosiPort, mosiPin, af);
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
     * @brief Set clock prescaler dynamically
     *
     * Disables SPI, updates BR[2:0] bits in CR1, re-enables.
     * Caller must hold the bus lock.
     */
    void setPrescaler(uint8_t prescaler) override {
        if (prescaler == m_prescaler) return;
        m_spi->CR1 &= ~SPI_CR1_SPE;
        m_spi->CR1 = (m_spi->CR1 & ~(0x7UL << 3)) | ((prescaler & 0x7) << 3);
        m_spi->CR1 |= SPI_CR1_SPE;
        m_prescaler = prescaler;
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

    /**
     * @brief Write-only transfer — DMA for SPI1 large writes, TX-only polled otherwise
     */
    void writeOnly(const uint8_t* data, size_t len) override {
        if (m_spi == SPI1 && len >= DMA_THRESHOLD) {
            writeOnlyDMA(data, len);
            return;
        }

        // TX-only polled (no RXNE waits)
        for (size_t i = 0; i < len; i++) {
            while (!(m_spi->SR & SPI_SR_TXE));
            *reinterpret_cast<volatile uint8_t*>(&m_spi->DR) = data[i];
        }
        while (m_spi->SR & SPI_SR_BSY);
        (void)m_spi->DR;
        (void)m_spi->SR;
    }

    /**
     * @brief Write repeated pattern — DMA for SPI1 large fills, TX-only polled otherwise
     */
    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) override {
        uint32_t totalBytes = static_cast<uint32_t>(patternLen) * repeatCount;

        // DMA path for SPI1 with large transfers
        if (m_spi == SPI1 && totalBytes >= DMA_THRESHOLD) {
            writeFillDMA(pattern, patternLen, totalBytes);
            return;
        }

        // Optimized TX-only polled path (skip per-byte RXNE waits)
        for (uint32_t i = 0; i < repeatCount; i++) {
            for (size_t j = 0; j < patternLen; j++) {
                while (!(m_spi->SR & SPI_SR_TXE));
                *reinterpret_cast<volatile uint8_t*>(&m_spi->DR) = pattern[j];
            }
        }
        while (m_spi->SR & SPI_SR_BSY);
        // Clear OVR: read DR then SR
        (void)m_spi->DR;
        (void)m_spi->SR;
    }

private:
    SPI_TypeDef* m_spi;
    SPIMode m_mode;
    uint8_t m_prescaler;
    ILock& m_lock;

    /**
     * @brief Configure GPIO pins for SPI alternate function (pins may be on different ports)
     */
    void initGPIO(GPIO_TypeDef* sckPort, uint8_t sckPin,
                  GPIO_TypeDef* misoPort, uint8_t misoPin,
                  GPIO_TypeDef* mosiPort, uint8_t mosiPin,
                  uint8_t af) {
        // Enable GPIO clocks (A, B, C)
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        configureAF(sckPort, sckPin, af);
        configureAF(misoPort, misoPin, af);
        configureAF(mosiPort, mosiPin, af);
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

    static constexpr size_t DMA_BUF_SIZE = 480;     // One LCD scanline (240 * 2 bytes)
    static constexpr uint32_t DMA_THRESHOLD = 32;    // Min bytes for DMA vs polled

    /**
     * @brief DMA-accelerated fill for SPI1 (DMA2 Stream 3 Channel 3)
     */
    void writeFillDMA(const uint8_t* pattern, size_t patternLen, uint32_t totalBytes) {
        static uint8_t dmaBuf[DMA_BUF_SIZE];

        // Fill buffer with repeated pattern
        uint32_t bufFill = 0;
        while (bufFill + patternLen <= DMA_BUF_SIZE) {
            for (size_t j = 0; j < patternLen; j++) {
                dmaBuf[bufFill++] = pattern[j];
            }
        }
        if (bufFill == 0) {
            // Pattern larger than buffer — fall back to polled
            ISPIBus::writeFill(pattern, patternLen, totalBytes / static_cast<uint32_t>(patternLen));
            return;
        }

        // Enable DMA2 clock
        RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

        // Disable stream before configuration
        DMA2_Stream3->CR = 0;
        while (DMA2_Stream3->CR & DMA_SxCR_EN);

        // Configure: Channel 3, Memory-to-Peripheral, byte size, MINC, high priority
        DMA2_Stream3->PAR = reinterpret_cast<uint32_t>(&m_spi->DR);
        DMA2_Stream3->FCR = 0;  // Direct mode (no FIFO)

        uint32_t cr = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1  // Channel 3
                    | DMA_SxCR_DIR_0                          // Memory-to-peripheral
                    | DMA_SxCR_MINC                           // Memory increment
                    | DMA_SxCR_PL_1;                          // High priority

        // Enable SPI TX DMA request
        m_spi->CR2 |= SPI_CR2_TXDMAEN;

        uint32_t remaining = totalBytes;
        while (remaining > 0) {
            uint32_t chunk = remaining;
            if (chunk > bufFill) chunk = bufFill;
            if (chunk > 65535) chunk = 65535;

            // Clear all interrupt flags for stream 3
            DMA2->LIFCR = DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3
                         | DMA_LIFCR_CTEIF3 | DMA_LIFCR_CDMEIF3
                         | DMA_LIFCR_CFEIF3;

            DMA2_Stream3->NDTR = chunk;
            DMA2_Stream3->M0AR = reinterpret_cast<uint32_t>(dmaBuf);
            DMA2_Stream3->CR = cr | DMA_SxCR_EN;

            // Poll for transfer complete
            while (!(DMA2->LISR & DMA_LISR_TCIF3));

            // Disable stream
            DMA2_Stream3->CR = 0;
            while (DMA2_Stream3->CR & DMA_SxCR_EN);

            remaining -= chunk;
        }

        // Disable SPI TX DMA
        m_spi->CR2 &= ~SPI_CR2_TXDMAEN;

        // Wait for SPI to finish transmitting last byte
        while (m_spi->SR & SPI_SR_BSY);

        // Clear OVR: read DR then SR
        (void)m_spi->DR;
        (void)m_spi->SR;
    }

    /**
     * @brief DMA-accelerated write for SPI1 — arbitrary data (not repeated pattern)
     *
     * Sends directly from the caller's buffer via DMA2 Stream 3 Channel 3.
     */
    void writeOnlyDMA(const uint8_t* data, size_t len) {
        RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

        DMA2_Stream3->CR = 0;
        while (DMA2_Stream3->CR & DMA_SxCR_EN);

        DMA2_Stream3->PAR = reinterpret_cast<uint32_t>(&m_spi->DR);
        DMA2_Stream3->FCR = 0;

        uint32_t cr = DMA_SxCR_CHSEL_0 | DMA_SxCR_CHSEL_1  // Channel 3
                    | DMA_SxCR_DIR_0                          // Memory-to-peripheral
                    | DMA_SxCR_MINC                           // Memory increment
                    | DMA_SxCR_PL_1;                          // High priority

        m_spi->CR2 |= SPI_CR2_TXDMAEN;

        const uint8_t* ptr = data;
        size_t remaining = len;
        while (remaining > 0) {
            uint32_t chunk = remaining > 65535 ? 65535 : static_cast<uint32_t>(remaining);

            DMA2->LIFCR = DMA_LIFCR_CTCIF3 | DMA_LIFCR_CHTIF3
                         | DMA_LIFCR_CTEIF3 | DMA_LIFCR_CDMEIF3
                         | DMA_LIFCR_CFEIF3;

            DMA2_Stream3->NDTR = chunk;
            DMA2_Stream3->M0AR = reinterpret_cast<uint32_t>(ptr);
            DMA2_Stream3->CR = cr | DMA_SxCR_EN;

            while (!(DMA2->LISR & DMA_LISR_TCIF3));

            DMA2_Stream3->CR = 0;
            while (DMA2_Stream3->CR & DMA_SxCR_EN);

            ptr += chunk;
            remaining -= chunk;
        }

        m_spi->CR2 &= ~SPI_CR2_TXDMAEN;
        while (m_spi->SR & SPI_SR_BSY);
        (void)m_spi->DR;
        (void)m_spi->SR;
    }
};

#endif // SPI_HARDWARE_HPP
