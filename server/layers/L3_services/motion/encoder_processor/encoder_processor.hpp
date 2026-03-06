/**
 * @file encoder_processor.hpp
 * @brief Encoder signal processing — position unwrapping, composable filter
 *        chain, and velocity quality assessment.
 *
 * Pure logic, no RTOS or hardware dependencies. Called by encoder_task
 * which handles DMA sampling and RTOS scheduling.
 *
 * Filter pipeline:
 *   raw velocity → EMA → SMA → Padé → Butterworth → Notch → Holt → deadband → output
 */

#pragma once

#include "harness/pins/iencoder_processing.hpp"
#include <stdint.h>

namespace Services {

// Import harness types into Services namespace for backward compatibility
using Harness::EncoderFilterConfig;
using Harness::ENC_FILT_EMA;
using Harness::ENC_FILT_SMA;
using Harness::ENC_FILT_PADE;
using Harness::ENC_FILT_BIQUAD;
using Harness::ENC_FILT_NOTCH;
using Harness::ENC_FILT_HOLT;

class EncoderProcessor : public Harness::IEncoderProcessing {
public:
    /**
     * @brief Process one raw 16-bit encoder count (position unwrapping).
     *
     * Converts 16-bit wrapping TIM4 count to 64-bit accumulated position
     * via signed delta. Also tracks edge timestamps for quality assessment.
     *
     * @param rawCount Current TIM4->CNT value
     * @param tickUs Current microsecond timestamp
     */
    void feedSample(uint16_t rawCount, uint32_t tickUs) override;

    /**
     * @brief Apply composable filter chain to raw velocity.
     *
     * Runs: EMA → SMA → Padé → Biquad → Notch → Holt → deadband.
     * Updates quality assessment based on edge age.
     *
     * @param rawVelocity Raw velocity from DMA measurement window (counts/sec)
     * @param tickUs Current microsecond timestamp
     * @return Filtered velocity (counts/sec)
     */
    int32_t filter(int32_t rawVelocity, uint32_t tickUs) override;

    /** @brief Reset position + all filter state */
    void reset() override;

    /** @brief Reset filter state only (keep position) */
    void resetFilters();

    void setFilterConfig(uint8_t flags, uint8_t emaAlpha, uint8_t smaWindow,
                         uint8_t padeGainPct, uint8_t padeMaxCorr,
                         uint8_t biquadCutoffHz, uint8_t notchCenterHz,
                         uint8_t notchQ10, uint8_t holtAlpha, uint8_t holtBeta) override;

    void getFilterConfig(EncoderFilterConfig& cfg) const override;

    /** @brief Legacy: set filter type (0=NONE, 1=EMA, 2=SMA) and parameter */
    void setFilter(uint8_t type, uint8_t param) override;

    /** @brief Legacy: get filter type and parameter */
    void getFilter(uint8_t& type, uint8_t& param) const override;

    // Accessors
    int64_t getPosition() const override { return m_accumulatedCount; }
    int32_t getFilteredVelocity() const { return m_filteredVelocity; }
    VelocityQuality getQuality() const override { return m_velQuality; }
    uint8_t getFilterFlags() const override { return m_filterFlags; }

private:
    // Position tracking
    int64_t m_accumulatedCount = 0;
    uint16_t m_lastRawCount = 0;

    // Filter configuration
    uint8_t m_filterFlags = ENC_FILT_EMA | ENC_FILT_SMA | ENC_FILT_PADE | ENC_FILT_BIQUAD;
    uint8_t m_emaAlpha = 200;
    uint8_t m_smaWindow = 8;
    uint8_t m_padeGainPct = 25;
    uint8_t m_padeMaxCorr = 50;
    uint8_t m_biquadCutoffHz = 5;
    uint8_t m_notchCenterHz = 25;
    uint8_t m_notchQ10 = 50;
    uint8_t m_holtAlpha = 51;
    uint8_t m_holtBeta = 13;

    // EMA state
    int32_t m_emaVelocity = 0;

    // SMA state
    static constexpr int SMA_MAX_WINDOW = 32;
    int32_t m_smaBuf[SMA_MAX_WINDOW] = {};
    int m_smaHead = 0;
    int m_smaCount = 0;

    // Padé [1/1] sharpener state
    float m_padeHistory[3] = {0.0f, 0.0f, 0.0f};
    int m_padeCount = 0;

    // Butterworth 2nd-order IIR low-pass (biquad) state
    float m_bqB0 = 0.0f, m_bqB1 = 0.0f, m_bqB2 = 0.0f;
    float m_bqA1 = 0.0f, m_bqA2 = 0.0f;
    float m_bqW0 = 0.0f, m_bqW1 = 0.0f;

    // Notch (band-reject) filter state
    float m_ntB0 = 0.0f, m_ntB1 = 0.0f, m_ntB2 = 0.0f;
    float m_ntA1 = 0.0f, m_ntA2 = 0.0f;
    float m_ntW0 = 0.0f, m_ntW1 = 0.0f;

    // Holt's double exponential smoothing state
    float m_holtLevel = 0.0f;
    float m_holtTrend = 0.0f;
    bool  m_holtInit = false;

    // Quality tracking
    uint32_t m_lastEdgeUs = 0;
    VelocityQuality m_velQuality = VelocityQuality::INVALID;
    int32_t m_filteredVelocity = 0;

    // Private filter application methods
    int32_t applyEMA(int32_t raw);
    int32_t applySMA(int32_t raw);
    int32_t applyPadeSharpen(int32_t input);
    int32_t applyBiquad(int32_t input);
    int32_t applyNotch(int32_t input);
    int32_t applyHolt(int32_t input);
    void computeBiquadCoeffs(uint8_t cutoffHz);
    void computeNotchCoeffs(uint8_t centerHz, uint8_t q10);
};

extern EncoderProcessor g_encoderProcessor;

} // namespace Services
