/**
 * @file iencoder_dispatcher.hpp
 * @brief Interface for encoder queries and filter config (CTRL:ENC namespace)
 */

#pragma once

#include "harness/pins/encoder_filter_config.hpp"
#include <stdint.h>

namespace Services {

class IEncoderDispatcher {
public:
    virtual ~IEncoderDispatcher() = default;

    /** @brief Alias for standalone Harness::EncoderFilterConfig (backward compatibility) */
    using EncFilterParams = Harness::EncoderFilterConfig;

    /** @brief POD mirror of encoder state snapshot */
    struct EncoderSnapshot {
        int64_t count;
        int32_t velocity;
        bool indexSeen;
        uint32_t indexTick;
        int32_t revolutions;
        uint32_t indexPeriodUs;
    };

    // Encoder state queries
    virtual bool isEncoderAvailable() = 0;
    virtual EncoderSnapshot getEncoderSnapshot() = 0;
    virtual void encoderResetCount() = 0;
    /// Position capture: freeze the DMA rings (samples, rate, whether a SYNC ring exists),
    /// page them out oldest first, resume sampling. False when no encoder is fitted.
    virtual bool captureFreeze(uint32_t& samples, uint16_t& rateHz, bool& sync) = 0;
    virtual bool captureRead(uint32_t i, uint16_t& enc, uint16_t& sync) = 0;
    virtual void captureResume() = 0;

    // Live runtime filter config
    virtual void encFilterGetConfig(EncFilterParams& out) = 0;
    virtual void encFilterSetConfig(const EncFilterParams& cfg) = 0;
    virtual void encFilterSetLegacy(uint8_t type, uint8_t param) = 0;

    // Persistent encoder filter config (stored in motor config flash)
    virtual void configSetEncFilter(uint8_t type, uint8_t alpha) = 0;
    virtual void configSetEncSmaWindow(uint8_t window) = 0;
    virtual void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                         uint8_t smaWindow, uint8_t measWindowMs,
                                         uint8_t rateDiv) = 0;
};

} // namespace Services
