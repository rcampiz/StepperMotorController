/**
 * @file ispi_bus.hpp
 * @brief Consumer port — L4 side of the Board::ISPIBus harness connection
 *
 * This is what L4 drivers need from the SPI bus. The harness adapter
 * bridges Board::ISPIBus (L5 provider) to Drivers::ISPIBus (this port).
 *
 * Drivers::ISPIBus is structurally identical to Board::ISPIBus — the
 * adapter is a trivial forwarder. The value is in namespace separation:
 * L4 code depends only on L4 types, never on L5 directly.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Drivers {

/// SPI clock polarity and phase modes (mirrors Board::SPIMode)
enum class SPIMode : uint8_t {
    Mode0 = 0,  // CPOL=0, CPHA=0
    Mode1 = 1,  // CPOL=0, CPHA=1
    Mode2 = 2,  // CPOL=1, CPHA=0
    Mode3 = 3   // CPOL=1, CPHA=1
};

/// SPI clock prescaler (mirrors Board::SPIPrescaler)
enum class SPIPrescaler : uint8_t {
    Div2   = 0,
    Div4   = 1,
    Div8   = 2,
    Div16  = 3,
    Div32  = 4,
    Div64  = 5,
    Div128 = 6,
    Div256 = 7
};

/// Abstract SPI bus interface — consumer port for L4 drivers
class ISPIBus {
public:
    virtual ~ISPIBus() = default;

    virtual void lock() = 0;
    virtual void unlock() = 0;

    virtual void setMode(SPIMode mode) = 0;
    virtual SPIMode getMode() const = 0;
    virtual void setPrescaler(SPIPrescaler prescaler) { (void)prescaler; }

    virtual uint8_t transfer(uint8_t data) = 0;
    virtual void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) = 0;

    void write(const uint8_t* data, size_t len) {
        transfer(data, nullptr, len);
    }

    void read(uint8_t* data, size_t len) {
        transfer(nullptr, data, len);
    }

    virtual void writeOnly(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            transfer(data[i]);
        }
    }

    virtual void startAsyncRead(uint8_t* data, size_t len) {
        read(data, len);
    }

    virtual void waitAsyncRead() {}

    virtual void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) {
        for (uint32_t i = 0; i < repeatCount; i++) {
            for (size_t j = 0; j < patternLen; j++) {
                transfer(pattern[j]);
            }
        }
    }
};

} // namespace Drivers
