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

#include "F_platform/hal/ispi_bus.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

#include <stdio.h>

class TracedSPIBus : public ISPIBus {
public:
    explicit TracedSPIBus(ISPIBus& real) : m_real(real) {}

    void lock() override {
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.lock");
        m_real.lock();
    }

    void unlock() override {
        m_real.unlock();
        ITrace::log(ITrace::L4_L5_SPI, "[L4<L5]", "spi.unlock");
    }

    void setMode(SPIMode mode) override {
        char detail[6];
        snprintf(detail, sizeof(detail), "M%u", static_cast<unsigned>(mode));
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.setMode", detail);
        m_real.setMode(mode);
    }

    SPIMode getMode() const override {
        return m_real.getMode();
    }

    void setPrescaler(uint8_t prescaler) override {
        m_real.setPrescaler(prescaler);
    }

    // Single-byte transfer: too frequent to trace
    uint8_t transfer(uint8_t data) override {
        return m_real.transfer(data);
    }

    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.xfer", detail);
        m_real.transfer(txBuf, rxBuf, len);
    }

    void writeOnly(const uint8_t* data, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.writeOnly", detail);
        m_real.writeOnly(data, len);
    }

    void startAsyncRead(uint8_t* data, size_t len) override {
        char detail[12];
        snprintf(detail, sizeof(detail), "%u", (unsigned)len);
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.asyncRead", detail);
        m_real.startAsyncRead(data, len);
    }

    void waitAsyncRead() override {
        m_real.waitAsyncRead();
    }

    void writeFill(const uint8_t* pattern, size_t patternLen, uint32_t repeatCount) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%ux%lu",
                 (unsigned)patternLen, (unsigned long)repeatCount);
        ITrace::log(ITrace::L4_L5_SPI, "[L4>L5]", "spi.fill", detail);
        m_real.writeFill(pattern, patternLen, repeatCount);
    }

private:
    ISPIBus& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
