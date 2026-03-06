/**
 * @file encoder_filter_config.hpp
 * @brief Standalone encoder filter configuration type
 *
 * Single source of truth for filter config struct and flag constants.
 * Used by encoder_task (consumer) and EncoderProcessor (provider).
 */

#pragma once

#include <stdint.h>

namespace Harness {

// Filter flag bits
constexpr uint8_t ENC_FILT_EMA    = 0x01;  // bit 0: EMA enabled
constexpr uint8_t ENC_FILT_SMA    = 0x02;  // bit 1: SMA enabled
constexpr uint8_t ENC_FILT_PADE   = 0x04;  // bit 2: Padé [1/1] sharpener enabled
constexpr uint8_t ENC_FILT_BIQUAD = 0x08;  // bit 3: Butterworth 2nd-order IIR low-pass
constexpr uint8_t ENC_FILT_NOTCH  = 0x10;  // bit 4: Notch (band-reject) filter
constexpr uint8_t ENC_FILT_HOLT   = 0x20;  // bit 5: Holt's double exponential smoothing

/**
 * @brief Composable encoder filter configuration
 *
 * Each filter stage is independently enabled via filterFlags.
 */
struct EncoderFilterConfig {
    // Static aliases for use as EncFilterParams::FILT_* (backward compatibility)
    static constexpr uint8_t FILT_EMA    = ENC_FILT_EMA;
    static constexpr uint8_t FILT_SMA    = ENC_FILT_SMA;
    static constexpr uint8_t FILT_PADE   = ENC_FILT_PADE;
    static constexpr uint8_t FILT_BIQUAD = ENC_FILT_BIQUAD;
    static constexpr uint8_t FILT_NOTCH  = ENC_FILT_NOTCH;
    static constexpr uint8_t FILT_HOLT   = ENC_FILT_HOLT;

    uint8_t  filterFlags;      // Bitfield: ENC_FILT_EMA | SMA | PADE | BIQUAD | NOTCH | HOLT
    uint8_t  emaAlpha;         // EMA smoothing (0-255, higher = smoother)
    uint8_t  smaWindow;        // SMA window (2-32, 0 = speed-adaptive)
    uint8_t  measWindowMs;     // Measurement window in ms (1-255, 0 = default 10)
    uint16_t sampleRateHz;     // DMA sample rate (100-10000, 0 = default 1000)
    uint8_t  padeGainPct;      // Padé correction strength (0-100%, 0 = default 50)
    uint8_t  padeMaxCorr;      // Max absolute correction (tps, 0 = default 50)
    uint8_t  biquadCutoffHz;   // Butterworth cutoff frequency (1-50 Hz, 0 = default 10)
    uint8_t  notchCenterHz;    // Notch center frequency (1-50 Hz, 0 = default 25)
    uint8_t  notchQ10;         // Notch Q factor × 10 (1-100 → Q 0.1-10.0, 0 = default 50)
    uint8_t  holtAlpha;        // Holt level smoothing (0-255, 0 = default 51 ≈ 0.20)
    uint8_t  holtBeta;         // Holt trend smoothing (0-255, 0 = default 13 ≈ 0.05)
};

} // namespace Harness
