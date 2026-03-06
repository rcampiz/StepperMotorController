/**
 * @file control_mode_dispatch.cpp
 * @brief L3 IControlModeDispatcher implementation
 */

#include "control_mode_dispatch.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

uint8_t ControlModeDispatch::getControlMode() {
    return static_cast<uint8_t>(motion.control.mode.getMode());
}

uint8_t ControlModeDispatch::getEncoderStatus() {
    return static_cast<uint8_t>(motion.control.mode.getEncoderStatus());
}

Harness::ServiceStatus ControlModeDispatch::setControlMode(uint8_t mode) {
    auto m = static_cast<ControlMode>(mode);
    if (motion.control.mode.setMode(m)) {
        return Harness::ServiceStatus::Ok;
    }
    if (m != ControlMode::OPEN_LOOP) {
        return Harness::ServiceStatus::EncoderRequired;
    }
    return Harness::ServiceStatus::InvalidState;
}

} // namespace Services
