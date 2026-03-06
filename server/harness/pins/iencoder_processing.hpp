/**
 * @file iencoder_processing.hpp
 * @brief Interface for encoder signal processing (position unwrapping, filter chain)
 *
 * Outbound harness interface: encoder_task calls through this to
 * EncoderProcessor (L3 service). Keeps task decoupled from service layer.
 */

#pragma once

#include "harness/pins/encoder_filter_config.hpp"
#include "harness/pins/velocity_quality.hpp"
#include <stdint.h>

namespace Harness {

class IEncoderProcessing {
public:
    virtual ~IEncoderProcessing() = default;

    /** @brief Process one raw 16-bit encoder count (position unwrapping) */
    virtual void feedSample(uint16_t rawCount, uint32_t tickUs) = 0;

    /** @brief Apply composable filter chain to raw velocity */
    virtual int32_t filter(int32_t rawVelocity, uint32_t tickUs) = 0;

    /** @brief Reset position + all filter state */
    virtual void reset() = 0;

    /** @brief Set composable filter parameters (resets filter state) */
    virtual void setFilterConfig(uint8_t flags, uint8_t emaAlpha, uint8_t smaWindow,
                                 uint8_t padeGainPct, uint8_t padeMaxCorr,
                                 uint8_t biquadCutoffHz, uint8_t notchCenterHz,
                                 uint8_t notchQ10, uint8_t holtAlpha, uint8_t holtBeta) = 0;

    /** @brief Get current filter configuration (filter params only) */
    virtual void getFilterConfig(EncoderFilterConfig& cfg) const = 0;

    /** @brief Legacy: set filter type (0=NONE, 1=EMA, 2=SMA) and parameter */
    virtual void setFilter(uint8_t type, uint8_t param) = 0;

    /** @brief Legacy: get filter type and parameter */
    virtual void getFilter(uint8_t& type, uint8_t& param) const = 0;

    /** @brief Get accumulated position (64-bit, no wrap) */
    virtual int64_t getPosition() const = 0;

    /** @brief Get velocity quality assessment */
    virtual VelocityQuality getQuality() const = 0;

    /** @brief Get active filter flags bitmask */
    virtual uint8_t getFilterFlags() const = 0;
};

} // namespace Harness
