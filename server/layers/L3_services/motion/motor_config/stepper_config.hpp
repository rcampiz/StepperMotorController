/**
 * @file stepper_config.hpp
 * @brief Stepper motor driver configuration view
 *
 * Header-only view into MotorConfigManager exposing only stepper-domain fields: the
 * driver's settings in physical units (and its capabilities), idle release, fault action,
 * full steps per revolution.
 */

#pragma once

#include "L3_services/motion/motor_config/motor_config.hpp"

namespace Services {

class StepperConfig {
public:
    explicit StepperConfig(MotorConfigManager& mgr) : m_mgr(mgr) {}

    // The driver's settings (physical) and what the driver can do
    const Harness::DriverCaps& caps() const { return m_mgr.caps(); }
    const Harness::DriverSettings& getDriverSettings() const { return m_mgr.getDriverSettings(); }
    void setDriverSettings(const Harness::DriverSettings& s) { m_mgr.setDriverSettings(s); }
    Harness::DriverSettings getEffectiveSettings() const { return m_mgr.getEffectiveSettings(); }
    bool toLegacyUnits(Drivers::LegacyDriverUnits& out) const { return m_mgr.toLegacyUnits(out); }
    bool setFromLegacyUnits(const Drivers::LegacyDriverUnits& in) { return m_mgr.setFromLegacyUnits(in); }

    // Drive mode and resolution
    bool    isCurrentMode() const { return m_mgr.isCurrentMode(); }
    uint8_t getDriveMode() const { return m_mgr.getDriveMode(); }
    void    setDriveMode(Harness::DriveMode m) { m_mgr.setDriveMode(m); }
    uint16_t getMicrosteps() const { return m_mgr.getMicrosteps(); }            ///< as asked
    uint16_t getEffectiveMicrosteps() const { return m_mgr.getEffectiveMicrosteps(); }   ///< in force
    void     setMicrosteps(uint16_t n) { m_mgr.setMicrosteps(n); }
    uint8_t  getFaultAlarms() const { return m_mgr.getFaultAlarms(); }

    uint16_t getIdleReleaseMs() const { return m_mgr.getIdleReleaseMs(); }
    void     setIdleReleaseMs(uint16_t ms) { m_mgr.setIdleReleaseMs(ms); }
    uint8_t  getFaultAction() const { return m_mgr.getFaultAction(); }
    void     setFaultAction(uint8_t a) { m_mgr.setFaultAction(a); }

    // Physical motor parameters
    uint16_t getFullStepsPerRev() const { return m_mgr.getFullStepsPerRev(); }
    void setFullStepsPerRev(uint16_t s) { m_mgr.setFullStepsPerRev(s); }
    uint32_t getMicrostepsPerRev() const { return m_mgr.getMicrostepsPerRev(); }

    const MotorConfig& getConfig() const { return m_mgr.getConfig(); }
    MotorConfig& getConfigMutable() { return m_mgr.getConfigMutable(); }

private:
    MotorConfigManager& m_mgr;
};

} // namespace Services
