/**
 * @file encoder_processor.cpp
 * @brief Encoder signal processing — filter chain and position tracking
 *
 * Extracted from encoder_task.cpp. All functions are pure logic with no
 * RTOS or hardware dependencies.
 */

#include "L3_services/motion/encoder_processor/encoder_processor.hpp"
#include <math.h>

// Task sample period (100 Hz) — used for biquad/notch coefficient computation.
// This is the rate at which filter() is called, not the DMA sample rate.
static constexpr uint32_t FILTER_SAMPLE_PERIOD_MS = 10;

namespace Services {

EncoderProcessor g_encoderProcessor;

// ============================================================================
// Position tracking
// ============================================================================

void EncoderProcessor::feedSample(uint16_t rawCount, uint32_t tickUs)
{
    auto delta = static_cast<int16_t>(rawCount - m_lastRawCount);
    m_accumulatedCount += delta;
    m_lastRawCount = rawCount;

    if (delta != 0) {
        m_lastEdgeUs = tickUs;
    }
}

// ============================================================================
// Filter chain
// ============================================================================

int32_t EncoderProcessor::filter(int32_t rawVelocity, uint32_t tickUs)
{
    int32_t filtered = rawVelocity;

    if (m_filterFlags & ENC_FILT_EMA) {
        filtered = applyEMA(filtered);
    } else {
        m_emaVelocity = filtered;
    }

    if (m_filterFlags & ENC_FILT_SMA) {
        filtered = applySMA(filtered);
    }

    if (m_filterFlags & ENC_FILT_PADE) {
        filtered = applyPadeSharpen(filtered);
    } else {
        m_padeHistory[0] = m_padeHistory[1] = m_padeHistory[2]
                         = static_cast<float>(filtered);
        m_padeCount = 0;
    }

    if (m_filterFlags & ENC_FILT_BIQUAD) {
        filtered = applyBiquad(filtered);
    } else {
        m_bqW0 = 0.0f;
        m_bqW1 = 0.0f;
    }

    if (m_filterFlags & ENC_FILT_NOTCH) {
        filtered = applyNotch(filtered);
    } else {
        m_ntW0 = 0.0f;
        m_ntW1 = 0.0f;
    }

    if (m_filterFlags & ENC_FILT_HOLT) {
        filtered = applyHolt(filtered);
    } else {
        m_holtLevel = static_cast<float>(filtered);
        m_holtTrend = 0.0f;
        m_holtInit = false;
    }

    // Deadband: suppress ±2 count/s noise at rest
    if (filtered > -3 && filtered < 3) {
        filtered = 0;
    }

    m_filteredVelocity = filtered;

    // Velocity quality assessment
    uint32_t edgeAgeUs = tickUs - m_lastEdgeUs;
    if (edgeAgeUs > 200000 && m_lastEdgeUs != 0) {
        m_velQuality = VelocityQuality::STALE;
    } else if (m_lastEdgeUs == 0) {
        m_velQuality = VelocityQuality::LOW_CONFIDENCE;
    } else {
        m_velQuality = VelocityQuality::GOOD;
    }

    return filtered;
}

// ============================================================================
// Reset
// ============================================================================

void EncoderProcessor::reset()
{
    m_accumulatedCount = 0;
    m_lastRawCount = 0;
    m_lastEdgeUs = 0;
    m_velQuality = VelocityQuality::INVALID;
    resetFilters();
}

void EncoderProcessor::resetFilters()
{
    m_filteredVelocity = 0;
    m_emaVelocity = 0;
    m_smaHead = 0;
    m_smaCount = 0;
    m_padeHistory[0] = m_padeHistory[1] = m_padeHistory[2] = 0.0f;
    m_padeCount = 0;
    m_bqW0 = m_bqW1 = 0.0f;
    m_ntW0 = m_ntW1 = 0.0f;
    m_holtLevel = 0.0f;
    m_holtTrend = 0.0f;
    m_holtInit = false;
}

// ============================================================================
// Configuration
// ============================================================================

void EncoderProcessor::setFilterConfig(uint8_t flags, uint8_t emaAlpha,
                                       uint8_t smaWindow,
                                       uint8_t padeGainPct, uint8_t padeMaxCorr,
                                       uint8_t biquadCutoffHz,
                                       uint8_t notchCenterHz, uint8_t notchQ10,
                                       uint8_t holtAlpha, uint8_t holtBeta)
{
    m_filterFlags = flags;
    m_emaAlpha = emaAlpha;
    m_smaWindow = smaWindow;
    m_padeGainPct = padeGainPct;
    m_padeMaxCorr = padeMaxCorr;
    m_biquadCutoffHz = biquadCutoffHz;
    m_notchCenterHz = notchCenterHz;
    m_notchQ10 = notchQ10;
    m_holtAlpha = holtAlpha;
    m_holtBeta = holtBeta;

    resetFilters();
    computeBiquadCoeffs(m_biquadCutoffHz);
    computeNotchCoeffs(m_notchCenterHz, m_notchQ10);
}

void EncoderProcessor::getFilterConfig(EncoderFilterConfig& cfg) const
{
    cfg.filterFlags = m_filterFlags;
    cfg.emaAlpha = m_emaAlpha;
    cfg.smaWindow = m_smaWindow;
    cfg.padeGainPct = m_padeGainPct;
    cfg.padeMaxCorr = m_padeMaxCorr;
    cfg.biquadCutoffHz = m_biquadCutoffHz;
    cfg.notchCenterHz = m_notchCenterHz;
    cfg.notchQ10 = m_notchQ10;
    cfg.holtAlpha = m_holtAlpha;
    cfg.holtBeta = m_holtBeta;
    // measWindowMs and sampleRateHz are NOT set here — task fills those in
}

void EncoderProcessor::setFilter(uint8_t type, uint8_t param)
{
    m_filterFlags = 0;
    m_emaAlpha = 0;

    if (type == 1) {
        m_filterFlags = ENC_FILT_EMA;
        m_emaAlpha = param;
    } else if (type == 2) {
        m_filterFlags = ENC_FILT_SMA;
        m_smaWindow = param;
    }

    m_emaVelocity = 0;
    m_smaHead = 0;
    m_smaCount = 0;
}

void EncoderProcessor::getFilter(uint8_t& type, uint8_t& param) const
{
    if (m_filterFlags & ENC_FILT_EMA) {
        type = 1;
        param = m_emaAlpha;
    } else if (m_filterFlags & ENC_FILT_SMA) {
        type = 2;
        param = m_smaWindow;
    } else {
        type = 0;
        param = 0;
    }
}

// ============================================================================
// EMA filter
// ============================================================================

int32_t EncoderProcessor::applyEMA(int32_t raw)
{
    if (m_emaAlpha == 0) {
        m_emaVelocity = raw;
        return raw;
    }
    int32_t weight = 256 - static_cast<int32_t>(m_emaAlpha);
    m_emaVelocity += (raw - m_emaVelocity) * weight / 256;
    return m_emaVelocity;
}

// ============================================================================
// SMA filter
// ============================================================================

int32_t EncoderProcessor::applySMA(int32_t raw)
{
    int window;
    if (m_smaWindow == 0) {
        // Adaptive window: longer at low speed, shorter at high speed
        int32_t absVel = (m_filteredVelocity < 0) ? -m_filteredVelocity : m_filteredVelocity;
        if (absVel < 100) {
            window = 8;
        } else if (absVel > 1000) {
            window = 3;
        } else {
            window = 8 - (((absVel - 100) * 5) / 900);
        }
    } else {
        window = static_cast<int>(m_smaWindow);
    }
    if (window < 2) window = 2;
    if (window > SMA_MAX_WINDOW) window = SMA_MAX_WINDOW;

    m_smaBuf[m_smaHead] = raw;
    m_smaHead = (m_smaHead + 1) % window;
    if (m_smaCount < window) m_smaCount++;
    if (m_smaCount > window) m_smaCount = window;

    int32_t sum = 0;
    for (int i = 0; i < m_smaCount; i++) sum += m_smaBuf[i];
    return sum / m_smaCount;
}

// ============================================================================
// Padé [1/1] sharpener — lag compensation
// ============================================================================

int32_t EncoderProcessor::applyPadeSharpen(int32_t input)
{
    m_padeHistory[0] = m_padeHistory[1];
    m_padeHistory[1] = m_padeHistory[2];
    m_padeHistory[2] = static_cast<float>(input);

    if (m_padeCount < 3) {
        m_padeCount++;
        return input;
    }

    float d1 = m_padeHistory[1] - m_padeHistory[0];
    float d2 = m_padeHistory[2] - m_padeHistory[1];
    float curvature = d1 - d2;

    if (curvature > -1e-6f && curvature < 1e-6f) {
        return input;
    }

    float correction = (d1 * d2) / curvature;

    uint8_t gain = (m_padeGainPct > 0) ? m_padeGainPct : 50;
    correction = correction * static_cast<float>(gain) / 100.0f;

    float maxC = static_cast<float>((m_padeMaxCorr > 0) ? m_padeMaxCorr : 50);
    if (correction > maxC)  correction = maxC;
    if (correction < -maxC) correction = -maxC;

    float result = m_padeHistory[1] + correction;
    return static_cast<int32_t>(result + (result >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Butterworth 2nd-order IIR low-pass
// ============================================================================

void EncoderProcessor::computeBiquadCoeffs(uint8_t cutoffHz)
{
    float fc = (cutoffHz > 0 && cutoffHz <= 50) ? static_cast<float>(cutoffHz) : 10.0f;
    float fs = 1000.0f / static_cast<float>(FILTER_SAMPLE_PERIOD_MS); // 100 Hz
    float K = tanf(3.14159265f * fc / fs);
    float K2 = K * K;
    float sqrt2 = 1.41421356f;
    float norm = 1.0f / (1.0f + sqrt2 * K + K2);

    m_bqB0 = K2 * norm;
    m_bqB1 = 2.0f * m_bqB0;
    m_bqB2 = m_bqB0;
    m_bqA1 = 2.0f * (K2 - 1.0f) * norm;
    m_bqA2 = (1.0f - sqrt2 * K + K2) * norm;

    m_bqW0 = 0.0f;
    m_bqW1 = 0.0f;
}

int32_t EncoderProcessor::applyBiquad(int32_t input)
{
    float x = static_cast<float>(input);
    float y = m_bqB0 * x + m_bqW0;
    m_bqW0 = m_bqB1 * x - m_bqA1 * y + m_bqW1;
    m_bqW1 = m_bqB2 * x - m_bqA2 * y;
    return static_cast<int32_t>(y + (y >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Notch (band-reject) filter
// ============================================================================

void EncoderProcessor::computeNotchCoeffs(uint8_t centerHz, uint8_t q10)
{
    float fc = (centerHz > 0 && centerHz <= 50) ? static_cast<float>(centerHz) : 25.0f;
    float Q = (q10 > 0 && q10 <= 100) ? static_cast<float>(q10) / 10.0f : 5.0f;
    float fs = 1000.0f / static_cast<float>(FILTER_SAMPLE_PERIOD_MS); // 100 Hz
    float w0 = 2.0f * 3.14159265f * fc / fs;
    float sinW = sinf(w0);
    float cosW = cosf(w0);
    float alpha = sinW / (2.0f * Q);
    float norm = 1.0f / (1.0f + alpha);

    m_ntB0 = norm;
    m_ntB1 = -2.0f * cosW * norm;
    m_ntB2 = norm;
    m_ntA1 = -2.0f * cosW * norm;
    m_ntA2 = (1.0f - alpha) * norm;

    m_ntW0 = 0.0f;
    m_ntW1 = 0.0f;
}

int32_t EncoderProcessor::applyNotch(int32_t input)
{
    float x = static_cast<float>(input);
    float y = m_ntB0 * x + m_ntW0;
    m_ntW0 = m_ntB1 * x - m_ntA1 * y + m_ntW1;
    m_ntW1 = m_ntB2 * x - m_ntA2 * y;
    return static_cast<int32_t>(y + (y >= 0.0f ? 0.5f : -0.5f));
}

// ============================================================================
// Holt's double exponential smoothing
// ============================================================================

int32_t EncoderProcessor::applyHolt(int32_t input)
{
    float x = static_cast<float>(input);
    float a = static_cast<float>((m_holtAlpha > 0) ? m_holtAlpha : 51) / 255.0f;
    float b = static_cast<float>((m_holtBeta > 0) ? m_holtBeta : 13) / 255.0f;

    if (!m_holtInit) {
        m_holtLevel = x;
        m_holtTrend = 0.0f;
        m_holtInit = true;
        return input;
    }

    float prevLevel = m_holtLevel;
    m_holtLevel = a * x + (1.0f - a) * (m_holtLevel + m_holtTrend);
    m_holtTrend = b * (m_holtLevel - prevLevel) + (1.0f - b) * m_holtTrend;

    float result = m_holtLevel + m_holtTrend;
    return static_cast<int32_t>(result + (result >= 0.0f ? 0.5f : -0.5f));
}

} // namespace Services
