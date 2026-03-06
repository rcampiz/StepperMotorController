/**
 * @file stepper_config.hpp
 * @brief Stepper motor driver configuration view
 *
 * Header-only view into MotorConfigManager exposing only stepper-domain fields:
 * KVAL, thresholds, motion parameters, step mode, fault enables, fullStepsPerRev.
 */

#pragma once

#include "L3_services/motion/motor_config/motor_config.hpp"

namespace Services {

class StepperConfig {
public:
    explicit StepperConfig(MotorConfigManager& mgr) : m_mgr(mgr) {}

    // KVAL
    uint8_t getKvalHold() const { return m_mgr.getKvalHold(); }
    uint8_t getKvalRun() const { return m_mgr.getKvalRun(); }
    uint8_t getKvalAcc() const { return m_mgr.getKvalAcc(); }
    uint8_t getKvalDec() const { return m_mgr.getKvalDec(); }
    void setKval(uint8_t h, uint8_t r, uint8_t a, uint8_t d) { m_mgr.setKval(h, r, a, d); }

    // Protection thresholds
    uint8_t getOcdThreshold() const { return m_mgr.getOcdThreshold(); }
    uint8_t getStallThreshold() const { return m_mgr.getStallThreshold(); }
    void setOcdThreshold(uint8_t t) { m_mgr.setOcdThreshold(t); }
    void setStallThreshold(uint8_t t) { m_mgr.setStallThreshold(t); }

    // Motion parameters
    uint16_t getAcceleration() const { return m_mgr.getAcceleration(); }
    uint16_t getDeceleration() const { return m_mgr.getDeceleration(); }
    uint16_t getMaxSpeed() const { return m_mgr.getMaxSpeed(); }
    void setMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
        m_mgr.setMotionParams(acc, dec, maxSpd);
    }

    // Fault handling
    FaultEnableFlags getFaultEnable() const { return m_mgr.getFaultEnable(); }
    void setFaultEnable(FaultEnableFlags f) { m_mgr.setFaultEnable(f); }
    void setFaultAction(uint8_t a) { m_mgr.setFaultAction(a); }

    // Step mode
    uint8_t getStepMode() const { return m_mgr.getStepMode(); }
    void setStepMode(uint8_t m) { m_mgr.setStepMode(m); }

    // Physical motor parameters
    uint16_t getFullStepsPerRev() const { return m_mgr.getFullStepsPerRev(); }
    void setFullStepsPerRev(uint16_t s) { m_mgr.setFullStepsPerRev(s); }
    uint32_t getMicrostepsPerRev() const { return m_mgr.getMicrostepsPerRev(); }

    // Bulk access (for applyConfig register writes and direct field edits)
    const MotorConfig& getConfig() const { return m_mgr.getConfig(); }
    MotorConfig& getConfigMutable() { return m_mgr.getConfigMutable(); }

private:
    MotorConfigManager& m_mgr;
};

} // namespace Services
