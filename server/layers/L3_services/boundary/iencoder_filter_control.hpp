/**
 * @file boundary/iencoder_filter_control.hpp
 * @brief Runtime control of the encoder velocity filter and measurement window (L3-internal interface)
 */

#pragma once

#include "harness/pins/iencoder_dispatcher.hpp"

namespace Services {

class IEncoderFilterControl {
public:
    virtual ~IEncoderFilterControl() = default;

    virtual void getConfig(Services::IEncoderDispatcher::EncFilterParams& out) = 0;
    virtual void setConfig(const Services::IEncoderDispatcher::EncFilterParams& cfg) = 0;
    virtual void setLegacy(uint8_t type, uint8_t param) = 0;
};

} // namespace Services
