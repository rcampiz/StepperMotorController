/**
 * @file boundary/iencoderfiltercontrol.hpp
 * @brief Consumer port — Services side of the Scheduler::IEncoderFilterControl harness connection
 *
 * Structurally identical to Scheduler::IEncoderFilterControl.
 * The harness adapter bridges the provider to this consumer port.
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
