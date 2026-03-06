/**
 * @file iencoder_dispatcher.hpp
 * @brief Interface for encoder queries and filter config (CTRL:ENC namespace)
 */

#pragma once

#include "harness/pins/encoder_filter_config.hpp"
#include <stdint.h>

namespace Harness {

class IEncoderDispatcher {
public:
    virtual ~IEncoderDispatcher() = default;

    /** @brief Alias for standalone EncoderFilterConfig (backward compatibility) */
    using EncFilterParams = EncoderFilterConfig;

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

} // namespace Harness
