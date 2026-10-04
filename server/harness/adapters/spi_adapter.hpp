/**
 * @file spi_adapter.hpp
 * @brief Harness adapter: Board::ISPIBus (L5) --> Drivers::ISPIBus (L4)
 *
 * Trivial forwarder — both interfaces are structurally identical.
 * Converts between Board:: and Drivers:: enum types via static_cast.
 */

#pragma once

#include "L5_board/boundary/ispi_bus.hpp"
#include "L4_drivers/boundary/ispi_bus.hpp"

namespace Harness {

class SPIAdapter : public Drivers::ISPIBus {
public:
    explicit SPIAdapter(Board::ISPIBus& provider) : m_provider(provider) {}

    void lock() override { m_provider.lock(); }
    void unlock() override { m_provider.unlock(); }

    void setMode(Drivers::SPIMode mode) override {
        m_provider.setMode(static_cast<Board::SPIMode>(mode));
    }

    Drivers::SPIMode getMode() const override {
        return static_cast<Drivers::SPIMode>(m_provider.getMode());
    }

    void setPrescaler(Drivers::SPIPrescaler prescaler) override {
        m_provider.setPrescaler(static_cast<Board::SPIPrescaler>(prescaler));
    }

    uint8_t transfer(uint8_t data) override {
        return m_provider.transfer(data);
    }

    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        m_provider.transfer(txBuf, rxBuf, len);
    }

    void writeOnly(const uint8_t* data, size_t len) override {
        m_provider.writeOnly(data, len);
    }

    void startAsyncRead(uint8_t* data, size_t len) override {
        m_provider.startAsyncRead(data, len);
    }

    void waitAsyncRead() override {
        m_provider.waitAsyncRead();
    }

    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) override {
        m_provider.writeFill(pattern, patternLen, repeatCount);
    }

private:
    Board::ISPIBus& m_provider;
};

} // namespace Harness
