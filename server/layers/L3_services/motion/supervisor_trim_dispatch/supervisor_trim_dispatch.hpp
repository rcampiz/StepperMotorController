/**
 * @file supervisor_trim_dispatch.hpp
 * @brief L3 implementation of ISupervisorTrimDispatcher
 */

#pragma once

#include "harness/pins/isupervisor_trim_dispatcher.hpp"

namespace Services {

class SupervisorTrimDispatch : public Harness::ISupervisorTrimDispatcher {
public:
    FollowThresholds getFollowThresholds() override;
    void setFollowThresholds(const FollowThresholds& t) override;
    void applySupervisorConfig() override;
    bool supervisorClearFault() override;
    SupervisorTelemetry getSupervisorTelemetry() override;
    TrimConfig getTrimConfig() override;
    void setTrimGains(int16_t kp100, int16_t ki100) override;
    void setTrimLimits(uint16_t outLim, uint16_t iLim) override;
    void setTrimMaxPct(int16_t val) override;
    void resetTrim() override;
    void applyTrimConfig() override;
    bool sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                     bool forward, uint16_t durationMs, uint16_t settleMs,
                     uint16_t freqMHz) override;
    void sysIdAbort() override;
    uint8_t sysIdGetPhase() override;
    uint16_t sysIdGetSampleCount() override;
    const Harness::SysIdSampleData* sysIdGetSamples(uint16_t& count) override;
};

} // namespace Services
