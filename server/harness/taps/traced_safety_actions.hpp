/**
 * @file traced_safety_actions.hpp
 * @brief Traced wrapper for ISafetyActions (L3->Foundation boundary)
 *
 * Logs safety-related calls via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "harness/pins/isafety_actions.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

namespace Harness {

class TracedSafetyActions : public Harness::ISafetyActions {
public:
    explicit TracedSafetyActions(Harness::ISafetyActions& real) : m_real(real) {}

    uint16_t getMotorStatusReg() override {
        uint16_t val = m_real.getMotorStatusReg();
        Harness::ITrace::logResult(Harness::ITrace::L3_F_SAFETY, "[L3<F]", "safety.getStatus",
                          static_cast<uint32_t>(val));
        return val;
    }

    void clearCommsTimeout() override {
        Harness::ITrace::log(Harness::ITrace::L3_F_SAFETY, "[L3>F]", "safety.clearTimeout");
        m_real.clearCommsTimeout();
    }

    uint32_t setHeartbeatTimeout(uint32_t ms) override {
        Harness::ITrace::log(Harness::ITrace::L3_F_SAFETY, "[L3>F]", "safety.setHbTimeout");
        return m_real.setHeartbeatTimeout(ms);
    }

    void heartbeatReceived(uint32_t seq) override {
        Harness::ITrace::log(Harness::ITrace::L3_F_SAFETY, "[L3>F]", "safety.heartbeat");
        m_real.heartbeatReceived(seq);
    }

    void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                            uint32_t& lastSeq, uint32_t& remainingMs,
                            bool& timedOut) override {
        m_real.getHeartbeatStatus(enabled, timeoutMs, lastSeq, remainingMs, timedOut);
    }

private:
    Harness::ISafetyActions& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
