/**
 * @file sd_control_mode.cpp
 * @brief ServiceDispatcher — IControlModeDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/motion/control_mode.hpp"

uint8_t ServiceDispatcher::getControlMode() {
    return static_cast<uint8_t>(Services::g_controlMode.getMode());
}

uint8_t ServiceDispatcher::getEncoderStatus() {
    return static_cast<uint8_t>(Services::g_controlMode.getEncoderStatus());
}

Comms::ServiceStatus ServiceDispatcher::setControlMode(uint8_t mode) {
    auto m = static_cast<Services::ControlMode>(mode);
    if (Services::g_controlMode.setMode(m)) {
        return Comms::ServiceStatus::Ok;
    }
    if (m != Services::ControlMode::OPEN_LOOP) {
        return Comms::ServiceStatus::EncoderRequired;
    }
    return Comms::ServiceStatus::InvalidState;
}
