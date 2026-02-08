/**
 * @file spi_bus.hpp
 * @brief SPI bus wrapper for bit-banged SPI manager
 *
 * This provides the same interface as the old hardware SPI driver,
 * but uses the bit-banged SPI implementation from SPIManager.
 * Allows PowerSTEP01 and LCD drivers to work unchanged.
 */

#ifndef SPI_BUS_HPP
#define SPI_BUS_HPP

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "drivers/spi_bitbang.hpp"

/**
 * @brief SPI bus wrapper that delegates to SPIBitBang
 *
 * Provides the same interface as the old hardware SPI driver
 * for compatibility with existing device drivers.
 */
class SPIBus {
public:
    // Prescaler kept for API compatibility (ignored - bit-bang uses cycle timing)
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
     *
     * Mode 0: CPOL=0, CPHA=0 - Clock idle low, sample on rising edge
     * Mode 1: CPOL=0, CPHA=1 - Clock idle low, sample on falling edge
     * Mode 2: CPOL=1, CPHA=0 - Clock idle high, sample on falling edge
     * Mode 3: CPOL=1, CPHA=1 - Clock idle high, sample on rising edge
     */
    enum class Mode : uint8_t {
        Mode0 = 0,  // CPOL=0, CPHA=0
        Mode1 = 1,  // CPOL=0, CPHA=1
        Mode2 = 2,  // CPOL=1, CPHA=0
        Mode3 = 3   // CPOL=1, CPHA=1
    };

    /**
     * @brief Construct SPIBus wrapper around SPIBitBang instance
     *
     * @param bitbang Reference to bit-banged SPI instance
     */
    explicit SPIBus(SPIBitBang& bitbang)
        : m_spi(bitbang)
    {
    }

    /**
     * @brief Acquire bus mutex
     * @param timeout Ticks to wait
     * @return true if lock acquired
     */
    bool lock(TickType_t timeout = portMAX_DELAY) {
        return m_spi.lock(timeout);
    }

    /**
     * @brief Release bus mutex
     */
    void unlock() {
        m_spi.unlock();
    }

    /**
     * @brief Switch SPI mode (CPOL/CPHA)
     * @param mode New SPI mode
     */
    void setMode(Mode mode) {
        m_spi.setMode(static_cast<SPIMode>(static_cast<uint8_t>(mode)));
    }

    /**
     * @brief Get current SPI mode
     * @return Current mode
     */
    Mode getMode() const {
        return static_cast<Mode>(static_cast<uint8_t>(m_spi.getMode()));
    }

    /**
     * @brief Transfer one byte (full duplex)
     * @param data Byte to send
     * @return Byte received
     */
    uint8_t transfer(uint8_t data) {
        return m_spi.transfer(data);
    }

    /**
     * @brief Transfer multiple bytes
     * @param txBuf TX buffer (or nullptr for read-only)
     * @param rxBuf RX buffer (or nullptr for write-only)
     * @param len Number of bytes
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

private:
    SPIBitBang& m_spi;
};

#endif // SPI_BUS_HPP
