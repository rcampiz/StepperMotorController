/**
 * @file control_mode_dispatch.hpp
 * @brief L3 implementation of IControlModeDispatcher
 */

#pragma once

#include "harness/pins/icontrol_mode_dispatcher.hpp"

namespace Services {

class ControlModeDispatch : public Harness::IControlModeDispatcher {
public:
    uint8_t getControlMode() override;
    uint8_t getEncoderStatus() override;
    Harness::ServiceStatus setControlMode(uint8_t mode) override;
};

} // namespace Services
