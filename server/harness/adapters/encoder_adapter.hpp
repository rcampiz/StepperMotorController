/**
 * @file encoder_adapter.hpp
 * @brief Harness adapter: Scheduler::IEncoder (tasks) --> Services::IEncoder (L3)
 *
 * Trivial forwarder — both interfaces are structurally identical.
 * Converts EncoderSnapshot between Scheduler:: and Services:: via field copy.
 */

#pragma once

#include "F_platform/tasks/boundary/iencoder.hpp"
#include "L3_services/boundary/iencoder.hpp"

namespace Harness {

class EncoderAdapter : public Services::IEncoder {
public:
    explicit EncoderAdapter(Scheduler::IEncoder& provider) : m_provider(provider) {}

    Services::EncoderSnapshot getState() override {
        Scheduler::EncoderSnapshot s = m_provider.getState();
        return {s.count, s.velocity, s.indexSeen, s.indexTick,
                s.revolutions, s.indexPeriodUs, s.velocityQuality};
    }

    int32_t getCount() override { return m_provider.getCount(); }
    void resetCount() override { m_provider.resetCount(); }
    void clearIndexFlag() override { m_provider.clearIndexFlag(); }
    bool isAvailable() override { return m_provider.isAvailable(); }

private:
    Scheduler::IEncoder& m_provider;
};

} // namespace Harness
