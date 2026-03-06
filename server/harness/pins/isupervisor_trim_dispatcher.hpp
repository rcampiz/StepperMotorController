/**
 * @file isupervisor_trim_dispatcher.hpp
 * @brief Interface for following supervisor, speed trim, and sysid (CTRL namespace)
 */

#pragma once

#include <stdint.h>

namespace Harness {

/** @brief Sysid sample data (mirrors L3 SysIdSample layout) */
struct SysIdSampleData {
    uint16_t time_ms;
    int16_t  setpoint_tps;
    int16_t  actual_tps;
    int16_t  pos_error;
};

class ISupervisorTrimDispatcher {
public:
    virtual ~ISupervisorTrimDispatcher() = default;

    struct FollowThresholds {
        int16_t moveErr;
        uint16_t moveTime;
        int16_t holdErr;
        uint16_t holdTime;
        int16_t hardLimit;
        uint8_t maxRetries;
    };

    struct SupervisorTelemetry {
        uint8_t state;
        uint8_t tier;
        int32_t posError;
        int32_t velError;
        int32_t setpoint;
        int16_t pidOutput;
        uint8_t retryCount;
        uint32_t errorDurationMs;
    };

    struct TrimConfig {
        int16_t kp100;
        int16_t ki100;
        int16_t kd100;
        uint16_t outputLimit;
        uint16_t integralLimit;
    };

    // Following supervisor
    virtual FollowThresholds getFollowThresholds() = 0;
    virtual void setFollowThresholds(const FollowThresholds& t) = 0;
    virtual void applySupervisorConfig() = 0;
    virtual bool supervisorClearFault() = 0;
    virtual SupervisorTelemetry getSupervisorTelemetry() = 0;

    // Speed trim
    virtual TrimConfig getTrimConfig() = 0;
    virtual void setTrimGains(int16_t kp100, int16_t ki100) = 0;
    virtual void setTrimLimits(uint16_t outLim, uint16_t iLim) = 0;
    virtual void setTrimMaxPct(int16_t val) = 0;
    virtual void resetTrim() = 0;
    virtual void applyTrimConfig() = 0;

    // System identification
    virtual bool sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                             bool forward, uint16_t durationMs, uint16_t settleMs,
                             uint16_t freqMHz) = 0;
    virtual void sysIdAbort() = 0;
    virtual uint8_t sysIdGetPhase() = 0;
    virtual uint16_t sysIdGetSampleCount() = 0;
    virtual const SysIdSampleData* sysIdGetSamples(uint16_t& count) = 0;
};

} // namespace Harness
