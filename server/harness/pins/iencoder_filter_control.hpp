/**
 * @file iencoder_filter_control.hpp
 * @brief Interface for runtime encoder filter configuration
 *
 * Signal net from L3 (dispatcher) down to F_platform (encoder task).
 * Implemented directly in encoder_task.cpp.
 */

#pragma once

#include "harness/pins/iencoder_dispatcher.hpp"

namespace Harness {

class IEncoderFilterControl {
public:
    virtual ~IEncoderFilterControl() = default;

    virtual void getConfig(IEncoderDispatcher::EncFilterParams& out) = 0;
    virtual void setConfig(const IEncoderDispatcher::EncFilterParams& cfg) = 0;
    virtual void setLegacy(uint8_t type, uint8_t param) = 0;
};

} // namespace Harness
