/**
 * @file iencoder_dispatcher.hpp
 * @brief Interface for encoder queries and filter config (CTRL:ENC namespace)
 */

#pragma once

#include <stdint.h>

namespace Comms {

class IEncoderDispatcher {
public:
    virtual ~IEncoderDispatcher() = default;

    /** @brief POD mirror of encoder filter config (no task header dependencies) */
    struct EncFilterParams {
        static constexpr uint8_t FILT_EMA    = 0x01;
        static constexpr uint8_t FILT_SMA    = 0x02;
        static constexpr uint8_t FILT_PADE   = 0x04;
        static constexpr uint8_t FILT_BIQUAD = 0x08;
        static constexpr uint8_t FILT_NOTCH  = 0x10;
        static constexpr uint8_t FILT_HOLT   = 0x20;

        uint8_t  filterFlags;
        uint8_t  emaAlpha;
        uint8_t  smaWindow;
        uint8_t  measWindowMs;
        uint16_t sampleRateHz;
        uint8_t  padeGainPct;
        uint8_t  padeMaxCorr;
        uint8_t  biquadCutoffHz;
        uint8_t  notchCenterHz;
        uint8_t  notchQ10;
        uint8_t  holtAlpha;
        uint8_t  holtBeta;
    };

    // Encoder state queries
    virtual bool isEncoderAvailable() = 0;
    virtual void getEncoderState(int32_t& count, int32_t& velocity, bool& indexSeen) = 0;
    virtual void getEncoderStateFull(int64_t& count, int32_t& velocity, bool& indexSeen,
                                      uint32_t& indexTick, int32_t& revolutions,
                                      uint32_t& indexPeriodUs) = 0;
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

} // namespace Comms
