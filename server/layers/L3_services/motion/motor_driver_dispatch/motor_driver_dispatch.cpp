/**
 * @file motor_driver_dispatch.cpp
 * @brief L3 IMotorDriverDispatcher implementation
 */

#include "motor_driver_dispatch.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

Harness::IMotorDriverDispatcher::FaultEnableFlags MotorDriverDispatch::getFaultEnable() {
    const auto& fe = motion.stepper.config.getFaultEnable();
    return {fe.ocd != 0, fe.thermalSD != 0, fe.thermalWarn != 0,
            fe.uvlo != 0, fe.stallA != 0, fe.stallB != 0, fe.cmdErr != 0};
}

void MotorDriverDispatch::motorReinit() {
    m_motorCtrl->reinit();
}

bool MotorDriverDispatch::motorApplyConfig() {
    return m_motorCtrl->applyConfig();
}

bool MotorDriverDispatch::motorGetDebugInfo(Harness::MotorDebugParams& out) {
    return m_motorCtrl->getDebugInfo(out);
}

bool MotorDriverDispatch::motorSetStepModeSafe(uint8_t mode, uint8_t& readback) {
    return m_motorCtrl->setStepModeSafe(mode, readback);
}

} // namespace Services
