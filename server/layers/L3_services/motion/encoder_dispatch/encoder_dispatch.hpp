/**
 * @file encoder_dispatch.hpp
 * @brief L3 implementation of IEncoderDispatcher
 *
 * Encoder state queries delegate to injected IEncoder harness pin.
 * Filter config uses injected IEncoderFilterControl + motion.encoder.config.
 */

#pragma once

#include "L3_services/boundary/iencoder_dispatcher.hpp"
#include "L3_services/boundary/iencoder.hpp"
#include "L3_services/boundary/iencoder_filter_control.hpp"

namespace Services {

class EncoderDispatch : public Services::IEncoderDispatcher {
public:
    EncoderDispatch() = default;
    EncoderDispatch(Services::IEncoder* enc, Services::IEncoderFilterControl* filt)
        : m_encoder(enc), m_encFilter(filt) {}

    void setEncoder(Services::IEncoder* e) { m_encoder = e; }
    void setEncFilter(Services::IEncoderFilterControl* f) { m_encFilter = f; }

    bool isEncoderAvailable() override;
    EncoderSnapshot getEncoderSnapshot() override;
    void encoderResetCount() override;
    bool captureFreeze(uint32_t& samples, uint16_t& rateHz, bool& sync) override;
    bool captureRead(uint32_t i, uint16_t& enc, uint16_t& sync) override;
    void captureResume() override;
    void encFilterGetConfig(EncFilterParams& out) override;
    void encFilterSetConfig(const EncFilterParams& cfg) override;
    void encFilterSetLegacy(uint8_t type, uint8_t param) override;
    void configSetEncFilter(uint8_t type, uint8_t alpha) override;
    void configSetEncSmaWindow(uint8_t window) override;
    void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                 uint8_t smaWindow, uint8_t measWindowMs,
                                 uint8_t rateDiv) override;

private:
    Services::IEncoder* m_encoder = nullptr;
    Services::IEncoderFilterControl* m_encFilter = nullptr;
};

} // namespace Services
