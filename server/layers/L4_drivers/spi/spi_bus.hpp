/**
 * @file spi_bus.hpp
 * @brief SPI bus wrapper for ISPIBus implementations
 *
 * Provides the PowerSTEP01/LCD driver interface on top of any ISPIBus
 * implementation (hardware SPI or bit-banged SPI).
 *
 * No RTOS types appear in this file.
 */

#ifndef SPI_BUS_HPP
#define SPI_BUS_HPP

#include <stdint.h>
#include <stddef.h>
#include "F_platform/interfaces/ispi_bus.hpp"

/**
 * @brief SPI bus wrapper that delegates to an ISPIBus implementation
 *
 * Provides a compatibility interface for existing device drivers
 * (PowerSTEP01, LCD) that works with either hardware or bit-banged SPI.
 */
class SPIBus {
public:
    // Prescaler kept for API compatibility (ignored - backend handles timing)
    enum class Prescaler : uint8_t {
        Div2 = 0,
        Div4 = 1,
        Div8 = 2,
        Div16 = 3,
        Div32 = 4,
        Div64 = 5,
        Div128 = 6,
        Div256 = 7
    };

    /**
     * @brief SPI clock polarity and phase modes
     */
    enum class Mode : uint8_t {
        Mode0 = 0,  // CPOL=0, CPHA=0
        Mode1 = 1,  // CPOL=0, CPHA=1
        Mode2 = 2,  // CPOL=1, CPHA=0
        Mode3 = 3   // CPOL=1, CPHA=1
    };

    /**
     * @brief Construct SPIBus wrapper around any ISPIBus implementation
     * @param bus Reference to ISPIBus (SPIBitBang or SPIHardware)
     */
    explicit SPIBus(ISPIBus& bus)
        : m_spi(bus)
    {
    }

    /**
     * @brief Acquire bus lock (blocks until available)
     */
    void lock() {
        m_spi.lock();
    }

    /**
     * @brief Release bus lock
     */
    void unlock() {
        m_spi.unlock();
    }

    /**
     * @brief Switch SPI mode (CPOL/CPHA)
     */
    void setMode(Mode mode) {
        m_spi.setMode(static_cast<SPIMode>(static_cast<uint8_t>(mode)));
    }

    /**
     * @brief Get current SPI mode
     */
    Mode getMode() const {
        return static_cast<Mode>(static_cast<uint8_t>(m_spi.getMode()));
    }

    /**
     * @brief Set clock prescaler (BR[2:0] value)
     * @param prescaler Prescaler enum value
     */
    void setPrescaler(Prescaler prescaler) {
        m_spi.setPrescaler(static_cast<uint8_t>(prescaler));
    }

    /**
     * @brief Transfer one byte (full duplex)
     */
    uint8_t transfer(uint8_t data) {
        return m_spi.transfer(data);
    }

    /**
     * @brief Transfer multiple bytes
     */
    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) {
        m_spi.transfer(txBuf, rxBuf, len);
    }

    /**
     * @brief Write multiple bytes (ignore RX)
     */
    void write(const uint8_t* data, size_t len) {
        m_spi.write(data, len);
    }

    /**
     * @brief Read multiple bytes (send 0xFF)
     */
    void read(uint8_t* data, size_t len) {
        m_spi.read(data, len);
    }

    /**
     * @brief Write-only transfer (TX only, no RXNE waits). DMA on SPI1.
     */
    void writeOnly(const uint8_t* data, size_t len) {
        m_spi.writeOnly(data, len);
    }

    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) {
        m_spi.writeFill(pattern, patternLen, repeatCount);
    }

    /**
     * @brief Start non-blocking read (DMA on SPI2, sync fallback otherwise)
     */
    void startAsyncRead(uint8_t* data, size_t len) {
        m_spi.startAsyncRead(data, len);
    }

    /**
     * @brief Wait for async read to complete
     */
    void waitAsyncRead() {
        m_spi.waitAsyncRead();
    }

private:
    ISPIBus& m_spi;
};

#endif // SPI_BUS_HPP
