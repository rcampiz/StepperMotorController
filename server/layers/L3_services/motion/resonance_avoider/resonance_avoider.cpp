/**
 * @file resonance_avoider.cpp
 * @brief L3 ResonanceAvoider implementation (see the header for the rules)
 */

#include "L3_services/motion/resonance_avoider/resonance_avoider.hpp"
#include <math.h>

namespace Services {

using Harness::ResonanceState;

void ResonanceAvoider::configure(const Harness::ResonanceConfig& cfg)
{
    m_cfg = cfg;
    m_unsupported = false;
    m_cutLocked = false;
    m_state = (cfg.enabled && cfg.f1AHz > 0.0f) ? ResonanceState::NOMINAL : ResonanceState::OFF;
    m_target = m_lastRunA;                 // re-decided on the next update
}

void ResonanceAvoider::resync(float runAInForce)
{
    m_written = runAInForce;
    m_target = runAInForce;
    m_lastRunA = runAInForce;
    m_cutLocked = false;
    if (m_state != ResonanceState::OFF) {
        m_state = ResonanceState::NOMINAL;
    }
}

void ResonanceAvoider::markUnsupported()
{
    m_unsupported = true;
    m_state = ResonanceState::OFF;
}

float ResonanceAvoider::naturalHz(float currentA) const
{
    if (!(m_cfg.f1AHz > 0.0f) || !(currentA > 0.0f)) {
        return 0.0f;
    }
    return m_cfg.f1AHz * powf(currentA, m_cfg.exponent);
}

float ResonanceAvoider::resonantFsps(float currentA, uint8_t k) const
{
    return (k == 0) ? 0.0f : 4.0f * naturalHz(currentA) / static_cast<float>(k);
}

float ResonanceAvoider::bandFraction() const
{
    const uint8_t pct = (m_cfg.bandPct != 0) ? m_cfg.bandPct : Harness::RES_DEFAULT_BAND_PCT;
    return static_cast<float>(pct) / 100.0f;
}

bool ResonanceAvoider::clearAt(float speed, float currentA, float margin) const
{
    const uint8_t mask = (m_cfg.harmonicMask != 0) ? m_cfg.harmonicMask : Harness::RES_DEFAULT_HARMONICS;
    static constexpr uint8_t KS[3] = {1, 2, 4};
    static constexpr uint8_t BITS[3] = {Harness::RES_HARMONIC_1, Harness::RES_HARMONIC_2,
                                        Harness::RES_HARMONIC_4};
    for (unsigned i = 0; i < 3; ++i) {
        if ((mask & BITS[i]) == 0u) {
            continue;
        }
        const float res = resonantFsps(currentA, KS[i]);
        if (res > 0.0f && fabsf(speed - res) <= margin * res) {
            return false;
        }
    }
    return true;
}

void ResonanceAvoider::choose(const ResonanceInput& in, float speed, float band)
{
    m_chooseSpeed = speed;
    m_chooseSteady = m_steadyNow;
    const float margin = band + HYST;
    const float step = (in.stepA > 0.0f) ? in.stepA : 0.01f;
    const uint8_t boost = (m_cfg.boostPct != 0) ? m_cfg.boostPct : Harness::RES_DEFAULT_BOOST_PCT;
    const uint8_t cut = (m_cfg.cutPct != 0) ? m_cfg.cutPct : Harness::RES_DEFAULT_CUT_PCT;

    // 1. the smallest raise that clears every band: more torque, nothing given up
    float hi = in.runA * (1.0f + static_cast<float>(boost) / 100.0f);
    if (in.maxA > 0.0f && hi > in.maxA) {
        hi = in.maxA;
    }
    for (int n = 1; n < 256; ++n) {
        const float i = in.runA + static_cast<float>(n) * step;
        if (i > hi + 1e-4f) {
            break;
        }
        if (clearAt(speed, i, margin)) {
            m_target = i;
            m_state = ResonanceState::BOOST;
            return;
        }
    }
    // 2. the smallest cut, only where the lag can be watched and it has not tripped here
    if (in.guardAvailable && !m_cutLocked && m_steadyNow) {
        float lo = in.runA * (1.0f - static_cast<float>(cut) / 100.0f);
        if (lo < step) {
            lo = step;
        }
        for (int n = 1; n < 256; ++n) {
            const float i = in.runA - static_cast<float>(n) * step;
            if (i < lo - 1e-4f) {
                break;
            }
            if (clearAt(speed, i, margin)) {
                m_target = i;
                m_state = ResonanceState::CUT;
                m_feBaselineValid = false;
                // the lag before the cut, if it was steady at this speed
                m_preCutValid = m_steadyFeSpeed > 0.0f && fabsf(speed - m_steadyFeSpeed) <= 0.01f * speed;
                m_preCutFe = m_steadyFe;
                return;
            }
        }
    }
    // 3. nothing allowed clears it
    m_target = in.runA;
    m_state = ResonanceState::UNAVOIDABLE;
}

bool ResonanceAvoider::cutLagTooLarge(const ResonanceInput& in)
{
    // Called at steady speed in a cut. The first steady call checks the lag against the
    // steady lag before the cut at this speed (the margin the cut used), then keeps it as the
    // reference for any growth during the cut.
    if (!(in.countsPerFullStep > 0.0f)) {
        return false;
    }
    const float limit = GUARD_FULL_STEPS * in.countsPerFullStep;
    if (!m_feBaselineValid) {
        m_feBaselineValid = true;
        m_feBaseline = m_feAvg;
        const bool entryTooLarge = m_preCutValid && fabsf(m_feAvg - m_preCutFe) > limit;
        m_preCutValid = false;
        return entryTooLarge;
    }
    return fabsf(m_feAvg - m_feBaseline) > limit;
}

ResonanceDecision ResonanceAvoider::update(const ResonanceInput& in)
{
    // The lag average runs whatever the state, so a cut starts from a settled baseline
    const float fe = static_cast<float>(in.followError);
    if (!m_feSeeded) {
        m_feAvg = fe;
        m_feSeeded = true;
    } else {
        m_feAvg += FE_ALPHA * (fe - m_feAvg);
    }
    const float spd = fabsf(in.speedFsps);
    if (spd > 0.0f && fabsf(spd - m_prevSpeed) <= STEADY_TOL * spd) {
        if (m_steady < 0xFFFFu) m_steady++;
    } else {
        m_steady = 0;
        m_feBaselineValid = false;         // a new speed: a new steady lag to compare with
    }
    m_prevSpeed = spd;
    const bool steady = m_steady >= STEADY_POLLS;
    m_steadyNow = steady;
    if (steady && m_state != ResonanceState::CUT) {
        m_steadyFe = m_feAvg;              // the reference a cut at this speed is judged by
        m_steadyFeSpeed = spd;
    }
    if (in.runA != m_lastRunA) {           // the configured current changed (an apply)
        m_lastRunA = in.runA;
        m_cutLocked = false;
        if (m_state != ResonanceState::OFF) {
            m_state = ResonanceState::NOMINAL;
        }
    }

    const float speed = fabsf(in.speedFsps);
    const bool active = m_cfg.enabled && m_cfg.f1AHz > 0.0f && !m_unsupported && in.runA > 0.0f;
    if (!active) {
        m_state = ResonanceState::OFF;
        m_target = in.runA;
    } else if (speed < MIN_SPEED_FSPS) {
        m_state = ResonanceState::NOMINAL;
        m_target = in.runA;
        m_cutLocked = false;
    } else {
        const float band = bandFraction();
        switch (m_state) {
            case ResonanceState::BOOST:
            case ResonanceState::CUT:
                if (clearAt(speed, in.runA, band + HYST)) {
                    m_state = ResonanceState::NOMINAL;           // left the band
                    m_target = in.runA;
                    m_cutLocked = false;
                } else if (m_state == ResonanceState::CUT && steady && cutLagTooLarge(in)) {
                    m_guardTrips++;                              // the lag grew: torque first
                    m_cutLocked = true;
                    m_target = in.runA;
                    m_state = ResonanceState::UNAVOIDABLE;
                } else if (!clearAt(speed, m_target, band)) {
                    choose(in, speed, band);                     // the speed moved into another band
                }
                break;
            case ResonanceState::UNAVOIDABLE:
                if (clearAt(speed, in.runA, band + HYST)) {
                    m_state = ResonanceState::NOMINAL;
                    m_target = in.runA;
                    m_cutLocked = false;
                } else if (fabsf(speed - m_chooseSpeed) > 0.005f * speed ||
                           (steady && !m_chooseSteady)) {
                    choose(in, speed, band);                     // moved, or settled: a cut may now do
                }
                break;
            default:                                             // OFF -> NOMINAL, NOMINAL
                m_state = ResonanceState::NOMINAL;
                m_target = in.runA;
                if (!clearAt(speed, in.runA, band)) {
                    choose(in, speed, band);
                }
                break;
        }
    }

    ResonanceDecision d{m_target, fabsf(m_target - m_written) > 1e-4f};
    if (d.write) {
        m_written = m_target;
        m_changes++;
        m_steady = 0;                      // a new current: let the lag settle before judging it
        m_feBaselineValid = false;
    }
    return d;
}

} // namespace Services
