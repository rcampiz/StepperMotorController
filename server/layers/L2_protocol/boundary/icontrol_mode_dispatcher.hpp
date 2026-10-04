/**
 * @file boundary/icontrolmodedispatcher.hpp
 * @brief Consumer port — Protocol side of the Services::IControlModeDispatcher harness connection
 *
 * Structurally identical to Services::IControlModeDispatcher.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Protocol {

class IControlModeDispatcher {
public:
    virtual ~IControlModeDispatcher() = default;

    virtual uint8_t getControlMode() = 0;
    virtual uint8_t getEncoderStatus() = 0;
    virtual Harness::ServiceStatus setControlMode(uint8_t mode) = 0;
};

} // namespace Protocol
