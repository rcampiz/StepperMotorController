/**
 * @file traced_encoder.hpp
 * @brief Traced wrapper for IEncoder (L3->L4 boundary)
 *
 * Logs encoder queries via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "L3_services/motion/iencoder.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

class TracedEncoder : public Services::IEncoder {
public:
    explicit TracedEncoder(Services::IEncoder& real) : m_real(real) {}

    Services::EncoderSnapshot getState() override {
        Services::EncoderSnapshot snap = m_real.getState();
        ITrace::logResult(ITrace::L3_L4_ENCODER, "[L3<L4]", "enc.getState",
                          static_cast<uint32_t>(snap.velocity));
        return snap;
    }

    int32_t getCount() override {
        int32_t c = m_real.getCount();
        ITrace::logResult(ITrace::L3_L4_ENCODER, "[L3<L4]", "enc.getCount",
                          static_cast<uint32_t>(c));
        return c;
    }

    void resetCount() override {
        ITrace::log(ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.resetCount");
        m_real.resetCount();
    }

    void clearIndexFlag() override {
        ITrace::log(ITrace::L3_L4_ENCODER, "[L3>L4]", "enc.clearIndex");
        m_real.clearIndexFlag();
    }

    bool isAvailable() override {
        return m_real.isAvailable();  // polled frequently, skip trace
    }

private:
    Services::IEncoder& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
