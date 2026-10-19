/**
 * @file motor_driver_dispatch.cpp
 * @brief L3 IMotorDriverDispatcher implementation
 */

#include "motor_driver_dispatch.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

Services::IMotorDriverDispatcher::FaultEnableFlags MotorDriverDispatch::getFaultEnable() {
    // Protocol 2 flags (stall per bridge) through the driver's chip units when it has them,
    // else from the chip-neutral alarm bits
    Drivers::LegacyDriverUnits u = {};
    if (motion.stepper.config.toLegacyUnits(u)) {
        return {(u.alarms & 1u) != 0, (u.alarms & 2u) != 0, (u.alarms & 4u) != 0, (u.alarms & 8u) != 0,
                (u.alarms & 16u) != 0, (u.alarms & 32u) != 0, (u.alarms & 64u) != 0};
    }
    const uint8_t f = motion.stepper.config.getFaultAlarms();
    const bool stall = (f & Harness::DriverFault::STALL) != 0;
    return {(f & Harness::DriverFault::OVERCURRENT) != 0, (f & Harness::DriverFault::THERMAL_SHUTDOWN) != 0,
            (f & Harness::DriverFault::THERMAL_WARNING) != 0, (f & Harness::DriverFault::UNDERVOLTAGE) != 0,
            stall, stall, (f & Harness::DriverFault::COMMAND_ERROR) != 0};
}

void MotorDriverDispatch::motorReinit() {
    m_motorCtrl->reinit();
}

bool MotorDriverDispatch::motorApplyConfig() {
    return m_motorCtrl->applyConfig();
}

bool MotorDriverDispatch::motorGetDebugInfo(Harness::MotorDebugParams& out, Harness::DriverDiagView view) {
    return m_motorCtrl->getDebugInfo(out, view);
}

bool MotorDriverDispatch::motorSetStepModeSafe(uint8_t mode, uint8_t& readback) {
    return m_motorCtrl->setStepModeSafe(mode, readback);
}

} // namespace Services
