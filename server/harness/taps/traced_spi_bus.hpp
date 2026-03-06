/**
 * @file traced_spi_bus.hpp
 * @brief Traced wrapper for ISPIBus (L4->L5 boundary)
 *
 * Logs SPI bus operations via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 *
 * High-frequency per-byte transfer() calls are NOT traced to avoid
 * flooding the ring buffer.  Only lock/unlock, bulk transfers, and
 * mode changes are logged.
 */

#pragma once

#include "harness/pins/ispi_bus.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

#include <stdio.h>

namespace Harness {

class TracedSPIBus : public Harness::ISPIBus {
public:
    explicit TracedSPIBus(Harness::ISPIBus& real) : m_real(real) {}

    void lock() override {
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.lock");
        m_real.lock();
    }

    void unlock() override {
        m_real.unlock();
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4<L5]", "spi.unlock");
    }

    void setMode(Harness::SPIMode mode) override {
        char detail[6];
        snprintf(detail, sizeof(detail), "M%u", static_cast<unsigned>(mode));
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.setMode", detail);
        m_real.setMode(mode);
    }

    Harness::SPIMode getMode() const override {
        return m_real.getMode();
    }

    void setPrescaler(Harness::SPIPrescaler prescaler) override {
        m_real.setPrescaler(prescaler);
    }

    // Single-byte transfer: too frequent to trace
    uint8_t transfer(uint8_t data) override {
        return m_real.transfer(data);
    }

    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.xfer", detail);
        m_real.transfer(txBuf, rxBuf, len);
    }

    void writeOnly(const uint8_t* data, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.writeOnly", detail);
        m_real.writeOnly(data, len);
    }

    void startAsyncRead(uint8_t* data, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.asyncRead", detail);
        m_real.startAsyncRead(data, len);
    }

    void waitAsyncRead() override {
        m_real.waitAsyncRead();
    }

    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%ux%lu",
                 (unsigned)patternLen, (unsigned long)repeatCount);
        Harness::ITrace::log(Harness::ITrace::L4_L5_SPI, "[L4>L5]", "spi.fill", detail);
        m_real.writeFill(pattern, patternLen, repeatCount);
    }

private:
    Harness::ISPIBus& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
