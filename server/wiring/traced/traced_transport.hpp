/**
 * @file traced_transport.hpp
 * @brief Traced wrapper for ITransport (L1<->L2 boundary)
 *
 * Logs key transport calls via ITrace, forwards to real implementation.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "F_platform/interfaces/itransport.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

class TracedTransport : public Comms::ITransport {
public:
    explicit TracedTransport(Comms::ITransport& real) : m_real(real) {}

    bool init() override {
        ITrace::log(ITrace::L1_L2_TRANSPORT, "[L1>L2]", "init");
        return m_real.init();
    }

    bool available() override {
        return m_real.available();  // too noisy to trace
    }

    size_t read(uint8_t* buffer, size_t maxLen) override {
        return m_real.read(buffer, maxLen);  // too frequent to trace
    }

    bool readByte(uint8_t& byte, uint32_t timeoutMs) override {
        return m_real.readByte(byte, timeoutMs);  // too frequent
    }

    size_t write(const uint8_t* data, size_t len) override {
        ITrace::log(ITrace::L1_L2_TRANSPORT, "[L2>L1]", "write");
        return m_real.write(data, len);
    }

    size_t print(const char* str) override {
        ITrace::log(ITrace::L1_L2_TRANSPORT, "[L2>L1]", "print", str);
        return m_real.print(str);
    }

    size_t println(const char* str) override {
        ITrace::log(ITrace::L1_L2_TRANSPORT, "[L2>L1]", "println", str);
        return m_real.println(str);
    }

    void flush() override {
        m_real.flush();
    }

    bool setBaudRate(uint32_t baudRate) override {
        ITrace::log(ITrace::L1_L2_TRANSPORT, "[L1>L2]", "setBaud");
        return m_real.setBaudRate(baudRate);
    }

private:
    Comms::ITransport& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
