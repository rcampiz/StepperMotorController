/**
 * @file icontrol_mode_dispatcher.hpp
 * @brief Interface for control mode operations (CTRL namespace)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Harness {

class IControlModeDispatcher {
public:
    virtual ~IControlModeDispatcher() = default;

    virtual uint8_t getControlMode() = 0;
    virtual uint8_t getEncoderStatus() = 0;
    virtual ServiceStatus setControlMode(uint8_t mode) = 0;
};

} // namespace Harness
