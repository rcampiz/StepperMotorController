/**
 * @file traced_safety_actions.hpp
 * @brief Traced wrapper for ISafetyActions (L3->Foundation boundary)
 *
 * Logs safety-related calls via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "F_platform/interfaces/isafety_actions.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

class TracedSafetyActions : public Services::ISafetyActions {
public:
    explicit TracedSafetyActions(Services::ISafetyActions& real) : m_real(real) {}

    uint16_t getMotorStatusReg() override {
        uint16_t val = m_real.getMotorStatusReg();
        ITrace::logResult(ITrace::L3_F_SAFETY, "[L3<F]", "safety.getStatus",
                          static_cast<uint32_t>(val));
        return val;
    }

    void clearCommsTimeout() override {
        ITrace::log(ITrace::L3_F_SAFETY, "[L3>F]", "safety.clearTimeout");
        m_real.clearCommsTimeout();
    }

    uint32_t setHeartbeatTimeout(uint32_t ms) override {
        ITrace::log(ITrace::L3_F_SAFETY, "[L3>F]", "safety.setHbTimeout");
        return m_real.setHeartbeatTimeout(ms);
    }

    void heartbeatReceived(uint32_t seq) override {
        ITrace::log(ITrace::L3_F_SAFETY, "[L3>F]", "safety.heartbeat");
        m_real.heartbeatReceived(seq);
    }

    void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                            uint32_t& lastSeq, uint32_t& remainingMs,
                            bool& timedOut) override {
        m_real.getHeartbeatStatus(enabled, timeoutMs, lastSeq, remainingMs, timedOut);
    }

private:
    Services::ISafetyActions& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
