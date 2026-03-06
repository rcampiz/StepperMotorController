/**
 * @file encoder_dispatch.hpp
 * @brief L3 implementation of IEncoderDispatcher
 *
 * Encoder state queries delegate to injected IEncoder harness pin.
 * Filter config uses injected IEncoderFilterControl + motion.encoder.config.
 */

#pragma once

#include "harness/pins/iencoder_dispatcher.hpp"
#include "harness/pins/iencoder.hpp"
#include "harness/pins/iencoder_filter_control.hpp"

namespace Services {

class EncoderDispatch : public Harness::IEncoderDispatcher {
public:
    EncoderDispatch() = default;
    EncoderDispatch(Harness::IEncoder* enc, Harness::IEncoderFilterControl* filt)
        : m_encoder(enc), m_encFilter(filt) {}

    void setEncoder(Harness::IEncoder* e) { m_encoder = e; }
    void setEncFilter(Harness::IEncoderFilterControl* f) { m_encFilter = f; }

    bool isEncoderAvailable() override;
    EncoderSnapshot getEncoderSnapshot() override;
    void encoderResetCount() override;
    void encFilterGetConfig(EncFilterParams& out) override;
    void encFilterSetConfig(const EncFilterParams& cfg) override;
    void encFilterSetLegacy(uint8_t type, uint8_t param) override;
    void configSetEncFilter(uint8_t type, uint8_t alpha) override;
    void configSetEncSmaWindow(uint8_t window) override;
    void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                 uint8_t smaWindow, uint8_t measWindowMs,
                                 uint8_t rateDiv) override;

private:
    Harness::IEncoder* m_encoder = nullptr;
    Harness::IEncoderFilterControl* m_encFilter = nullptr;
};

} // namespace Services
