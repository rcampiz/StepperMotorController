/**
 * @file icontrol_mode_dispatcher.hpp
 * @brief Interface for control mode operations (CTRL namespace)
 */

#pragma once

#include "F_platform/interfaces/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class IControlModeDispatcher {
public:
    virtual ~IControlModeDispatcher() = default;

    virtual uint8_t getControlMode() = 0;
    virtual const char* controlModeString() = 0;
    virtual uint8_t getEncoderStatus() = 0;
    virtual const char* encoderStatusString() = 0;
    virtual DispatchResult setControlMode(const char* modeName) = 0;
};

} // namespace Comms
