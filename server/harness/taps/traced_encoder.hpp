/**
 * @file traced_encoder.hpp
 * @brief Traced wrapper for Drivers::IEncoderHardware (L3->L4 boundary)
 *
 * Logs the infrequent control calls (reset, clearIndexFlag, restartSampling) and index
 * observations. Per-sample ring reads are not traced (called every 10 ms in a loop).
 * Pure observer. Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "L4_drivers/boundary/iencoder_hardware.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

#include <stdio.h>

namespace Harness {

class TracedEncoder : public Drivers::IEncoderHardware {
public:
    explicit TracedEncoder(Drivers::IEncoderHardware& real) : m_real(real) {}

    bool isReady() const override { return m_real.isReady(); }
    int32_t getCount() const override { return m_real.getCount(); }
    void reset() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.reset");
        m_real.reset();
    }

    bool isIndexSeen() const override { return m_real.isIndexSeen(); }
    void clearIndexFlag() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.clearIndex");
        m_real.clearIndexFlag();
    }
    uint32_t getIndexTick() const override { return m_real.getIndexTick(); }
    int32_t getRevolutions() const override { return m_real.getRevolutions(); }
    uint32_t getIndexPeriodUs() const override { return m_real.getIndexPeriodUs(); }
    void indexISR(uint32_t tickMs, uint32_t tickUs) override { m_real.indexISR(tickMs, tickUs); }

    const uint16_t* sampleBuffer() const override { return m_real.sampleBuffer(); }
    uint32_t sampleBufferSize() const override { return m_real.sampleBufferSize(); }
    uint32_t sampleWriteIndex() const override { return m_real.sampleWriteIndex(); }
    uint16_t sampleRateHz() const override { return m_real.sampleRateHz(); }
    void restartSampling(uint16_t hz) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%uHz", (unsigned)hz);
        Harness::ITrace::log(Harness::ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.restart", detail);
        m_real.restartSampling(hz);
    }

private:
    Drivers::IEncoderHardware& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
