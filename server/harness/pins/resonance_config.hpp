/**
 * @file resonance_config.hpp
 * @brief The resonance avoider's model and limits — boundary contract type (pure data)
 *
 * The motor and its load ring at a natural frequency set by the run current:
 * f_n(I) = f1AHz x I^exponent (I in amps). The steps excite it at harmonics of the electrical
 * frequency (full steps/s / 4), so a resonance sits at the speed where harmonic k meets f_n.
 * The avoider moves f_n away from the speed in use by changing the run current live.
 * Profiled by the client's resonance_profile scenario (bench 2026-10-21: f1AHz 137,
 * exponent 0.51). Set by CTRL:RES:CFG, stored in the motor config (CTRL:RES:SAVE).
 */
#pragma once

#include <stdint.h>

namespace Harness {

constexpr uint8_t RES_HARMONIC_1 = 0x01;   // the electrical frequency
constexpr uint8_t RES_HARMONIC_2 = 0x02;   // twice it (half the full-step rate): the strongest
constexpr uint8_t RES_HARMONIC_4 = 0x04;   // four times it (the full-step rate)

constexpr uint8_t RES_DEFAULT_HARMONICS = RES_HARMONIC_1 | RES_HARMONIC_2 | RES_HARMONIC_4;
constexpr uint8_t RES_DEFAULT_BAND_PCT  = 8;    // avoided band: +-8 % of the resonant speed
constexpr uint8_t RES_DEFAULT_BOOST_PCT = 40;   // largest raise above the run current (bench:
                                                // the k = 2 band needs +31 % to clear upwards)
constexpr uint8_t RES_DEFAULT_CUT_PCT   = 30;   // largest cut below it (encoder-guarded)

struct ResonanceConfig {
    bool    enabled;
    uint8_t harmonicMask;   ///< RES_HARMONIC_* bits (0 = the default set)
    float   f1AHz;          ///< natural frequency at 1 A, Hz (0: no model, the avoider idles)
    float   exponent;       ///< f_n ~ I^exponent
    uint8_t bandPct;        ///< half-width of an avoided band, % of its speed (0 = default)
    uint8_t boostPct;       ///< largest raise, % of the run current (0 = default)
    uint8_t cutPct;         ///< largest cut, % of the run current (0 = default)
};

enum class ResonanceState : uint8_t {
    OFF = 0,          ///< disabled, no model, or the drive cannot set its current live
    NOMINAL = 1,      ///< the configured run current (the speed is clear of every band)
    BOOST = 2,        ///< raised: the resonance moved above the speed
    CUT = 3,          ///< lowered: the resonance moved below the speed (lag-guarded)
    UNAVOIDABLE = 4,  ///< in a band and no allowed current clears it: run current kept
};

} // namespace Harness
