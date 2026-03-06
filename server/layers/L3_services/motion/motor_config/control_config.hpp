/**
 * @file control_config.hpp
 * @brief Motion control configuration view
 *
 * Header-only view into MotorConfigManager exposing only control-domain fields:
 * PID/trim gains, output limits, following error supervisor thresholds.
 */

#pragma once

#include "L3_services/motion/motor_config/motor_config.hpp"

namespace Services {

class ControlConfig {
public:
    explicit ControlConfig(MotorConfigManager& mgr) : m_mgr(mgr) {}

    // PID / speed-trim gains (raw × 100)
    int16_t getPidKp100() const { return m_mgr.getPidKp100(); }
    int16_t getPidKi100() const { return m_mgr.getPidKi100(); }
    int16_t getPidKd100() const { return m_mgr.getPidKd100(); }

    // PID gains as float
    float getPidKp() const { return m_mgr.getPidKp(); }
    float getPidKi() const { return m_mgr.getPidKi(); }
    float getPidKd() const { return m_mgr.getPidKd(); }

    // Output limits
    uint16_t getPidOutputLimit() const { return m_mgr.getPidOutputLimit(); }
    uint16_t getPidIntegralLimit() const { return m_mgr.getPidIntegralLimit(); }

    // Speed-trim convenience
    float getTrimKp() const { return m_mgr.getTrimKp(); }
    float getTrimKi() const { return m_mgr.getTrimKi(); }
    uint8_t getTrimMaxPercent() const { return m_mgr.getTrimMaxPercent(); }
    uint16_t getTrimOutputLimit() const { return m_mgr.getTrimOutputLimit(); }
    uint16_t getTrimIntegralLimit() const { return m_mgr.getTrimIntegralLimit(); }

    // Following error supervisor thresholds
    int16_t getFollowMoveError() const { return m_mgr.getFollowMoveError(); }
    uint16_t getFollowMoveTimeMs() const { return m_mgr.getFollowMoveTimeMs(); }
    int16_t getFollowHoldError() const { return m_mgr.getFollowHoldError(); }
    uint16_t getFollowHoldTimeMs() const { return m_mgr.getFollowHoldTimeMs(); }
    int16_t getFollowHardLimit() const { return m_mgr.getFollowHardLimit(); }
    uint8_t getFollowMaxRetries() const { return m_mgr.getFollowMaxRetries(); }

    // Setters
    void setPidGains(int16_t kp100, int16_t ki100, int16_t kd100) {
        m_mgr.setPidGains(kp100, ki100, kd100);
    }
    void setPidLimits(uint16_t outputLimit, uint16_t integralLimit) {
        m_mgr.setPidLimits(outputLimit, integralLimit);
    }
    void setFollowThresholds(int16_t moveErr, uint16_t moveTimeMs,
                             int16_t holdErr, uint16_t holdTimeMs,
                             int16_t hardLimit, uint8_t maxRetries) {
        m_mgr.setFollowThresholds(moveErr, moveTimeMs, holdErr, holdTimeMs,
                                   hardLimit, maxRetries);
    }

private:
    MotorConfigManager& m_mgr;
};

} // namespace Services
