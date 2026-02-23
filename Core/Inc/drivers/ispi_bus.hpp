/**
 * @file ispi_bus.hpp
 * @brief Abstract SPI bus interface
 *
 * Allows switching between hardware SPI and bit-banged SPI implementations.
 * Both PowerSTEP01 (Mode 3) and LCD (Mode 0) can use the same interface.
 *
 * Locking is provided by an ILock injected at construction time.
 * No RTOS types appear in this interface.
 */

#ifndef ISPI_BUS_HPP
#define ISPI_BUS_HPP

#include <stdint.h>
#include <stddef.h>

/**
 * @brief SPI clock polarity and phase modes
 *
 * Mode 0: CPOL=0, CPHA=0 - Clock idle low, sample on rising edge (LCD/NOR flash)
 * Mode 1: CPOL=0, CPHA=1 - Clock idle low, sample on falling edge
 * Mode 2: CPOL=1, CPHA=0 - Clock idle high, sample on falling edge
 * Mode 3: CPOL=1, CPHA=1 - Clock idle high, sample on rising edge (powerSTEP01)
 */
enum class SPIMode : uint8_t {
    Mode0 = 0,  // CPOL=0, CPHA=0
    Mode1 = 1,  // CPOL=0, CPHA=1
    Mode2 = 2,  // CPOL=1, CPHA=0
    Mode3 = 3   // CPOL=1, CPHA=1
};

/**
 * @brief Abstract SPI bus interface
 *
 * Implement this interface for hardware SPI or bit-banged SPI.
 * Devices using this interface can work with either implementation.
 */
class ISPIBus {
public:
    virtual ~ISPIBus() = default;

    /**
     * @brief Acquire bus lock (blocks until available)
     */
    virtual void lock() = 0;

    /**
     * @brief Release bus lock
     */
    virtual void unlock() = 0;

    /**
     * @brief Set SPI mode (CPOL/CPHA)
     * @param mode SPI mode to set
     */
    virtual void setMode(SPIMode mode) = 0;

    /**
     * @brief Get current SPI mode
     */
    virtual SPIMode getMode() const = 0;

    /**
     * @brief Set clock prescaler (BR[2:0] value)
     * @param prescaler 0=/2, 1=/4, 2=/8, 3=/16, 4=/32, 5=/64, 6=/128, 7=/256
     *
     * Default implementation is a no-op (bit-banged SPI ignores prescaler).
     */
    virtual void setPrescaler(uint8_t prescaler) { (void)prescaler; }

    /**
     * @brief Transfer one byte (full duplex)
     * @param data Byte to send
     * @return Byte received
     */
    virtual uint8_t transfer(uint8_t data) = 0;

    /**
     * @brief Transfer multiple bytes
     * @param txBuf Data to send (nullptr to send 0xFF)
     * @param rxBuf Buffer for received data (nullptr to discard)
     * @param len Number of bytes to transfer
     */
    virtual void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) = 0;

    /**
     * @brief Write multiple bytes (discard received)
     */
    void write(const uint8_t* data, size_t len) {
        transfer(data, nullptr, len);
    }

    /**
     * @brief Read multiple bytes (send 0xFF)
     */
    void read(uint8_t* data, size_t len) {
        transfer(nullptr, data, len);
    }

    /**
     * @brief Write-only transfer (TX only, discard RX, no per-byte RXNE wait)
     *
     * Faster than write() for output-only devices (e.g. LCD).
     * Default: per-byte transfer. SPIHardware overrides with TX-only polled
     * and DMA for SPI1.
     *
     * @param data  Source buffer
     * @param len   Bytes to write
     */
    virtual void writeOnly(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            transfer(data[i]);
        }
    }

    /**
     * @brief Start non-blocking read (for double-buffering)
     *
     * Default: synchronous fallback (blocks until complete).
     * SPIHardware overrides with DMA for SPI2.
     * Caller must call waitAsyncRead() before using the data.
     */
    virtual void startAsyncRead(uint8_t* data, size_t len) {
        read(data, len);
    }

    /**
     * @brief Wait for async read started by startAsyncRead() to complete
     *
     * Default: no-op (startAsyncRead was synchronous).
     */
    virtual void waitAsyncRead() {}

    /**
     * @brief Write a repeated pattern (e.g., solid-color LCD fills)
     * @param pattern Pattern bytes to repeat
     * @param patternLen Length of pattern in bytes
     * @param repeatCount Number of times to repeat the pattern
     *
     * Default: polled byte-by-byte. SPIHardware overrides with DMA for SPI1.
     */
    virtual void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) {
        for (uint32_t i = 0; i < repeatCount; i++) {
            for (size_t j = 0; j < patternLen; j++) {
                transfer(pattern[j]);
            }
        }
    }
};

#endif // ISPI_BUS_HPP
