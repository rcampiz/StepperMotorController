/**
 * @file traced_encoder.hpp
 * @brief Traced wrapper for IEncoder (L3->L4 boundary)
 *
 * Logs encoder queries via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "harness/pins/iencoder.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

namespace Harness {

class TracedEncoder : public Harness::IEncoder {
public:
    explicit TracedEncoder(Harness::IEncoder& real) : m_real(real) {}

    Harness::EncoderSnapshot getState() override {
        Harness::EncoderSnapshot snap = m_real.getState();
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_ENCODER, "[L3<L4]", "enc.getState",
                          static_cast<uint32_t>(snap.velocity));
        return snap;
    }

    int32_t getCount() override {
        int32_t c = m_real.getCount();
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_ENCODER, "[L3<L4]", "enc.getCount",
                          static_cast<uint32_t>(c));
        return c;
    }

    void resetCount() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.resetCount");
        m_real.resetCount();
    }

    void clearIndexFlag() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.clearIndex");
        m_real.clearIndexFlag();
    }

    bool isAvailable() override {
        return m_real.isAvailable();  // polled frequently, skip trace
    }

private:
    Harness::IEncoder& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
