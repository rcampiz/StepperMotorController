/**
 * @file resonance_avoider.hpp
 * @brief L3 ResonanceAvoider — keeps the mechanical resonance away from the speed in use
 *
 * Pure logic (no RTOS, no hardware), driven by MotorController::periodicUpdate with the
 * driver's speed and the following error, and answering the run current to put in force.
 *
 * Model (Harness::ResonanceConfig): f_n(I) = f1AHz x I^exponent; harmonic k of the electrical
 * frequency (speed / 4, full steps/s) meets it at speed 4 f_n / k. A band of +-bandPct % around
 * each such speed is avoided. Sacrificing as little as possible:
 *   1. outside every band at the configured run current: that current (nothing changes);
 *   2. in a band: the smallest raise (driver steps, up to +boostPct %) that clears every band
 *      with a margin: more torque, the resonance moved above the speed;
 *   3. no raise clears it: the smallest cut (down to -cutPct %), only with an encoder and only
 *      at steady speed (a joystick changes the speed all the time: while it changes, the chip
 *      runs on its accel / decel currents anyway, and torque is what the operator needs); the
 *      following error, averaged, is judged at steady speed only (a ramp moves the lag by
 *      itself) and the cut is dropped (and not tried again in this band) when the lag is
 *      GUARD_FULL_STEPS above the steady lag before the cut at the same speed, or grows by
 *      that much during the cut;
 *   4. nothing clears it: the run current, state UNAVOIDABLE.
 * Back to the run current once the speed is clear of the band by the hysteresis margin.
 */
#pragma once

#include "harness/pins/resonance_config.hpp"
#include <stdint.h>

namespace Services {

struct ResonanceInput {
    float   speedFsps;          ///< speed now, full steps/s (sign ignored)
    float   runA;               ///< the configured run current (current mode), amps
    float   stepA;              ///< the driver's current resolution, amps
    float   maxA;               ///< the driver's largest current, amps
    bool    guardAvailable;     ///< an encoder is fitted: a cut may be tried
    int32_t followError;        ///< commanded minus measured, encoder counts (any offset)
    float   countsPerFullStep;  ///< encoder counts per full step
};

struct ResonanceDecision {
    float runA;                 ///< the run current to have in force
    bool  write;                ///< it differs from the last one written
};

class ResonanceAvoider {
public:
    static constexpr float HYST = 0.03f;              ///< clear a band by 3 % more than its half-width
    static constexpr float GUARD_FULL_STEPS = 0.35f;  ///< extra lag that ends a cut
    static constexpr float MIN_SPEED_FSPS = 10.0f;    ///< below: nominal (standstill, crawling)
    /// Following-error average per call: 200 ms at the 5 ms poll. The error wanders once per
    /// revolution by +-3.5 counts (bench 2026-10-21 at 85 RPM: encoder / coupling runout), which
    /// a 50 ms average followed into false trips; 200 ms halves it at these speeds.
    static constexpr float FE_ALPHA = 0.025f;
    static constexpr uint16_t STEADY_POLLS = 60;      ///< steady speed and current this many calls (300 ms)
                                                      ///< before the lag counts: the rotor and the average
                                                      ///< settle after a current change
    static constexpr float STEADY_TOL = 0.003f;       ///< call-to-call speed change still "steady"

    void configure(const Harness::ResonanceConfig& cfg);
    /// The settings were applied (the driver wrote the configured run current): start again
    void resync(float runAInForce);
    /// The drive cannot set its current live (voltage mode): idle until configured again
    void markUnsupported();

    ResonanceDecision update(const ResonanceInput& in);

    Harness::ResonanceState state() const { return m_state; }
    float currentA() const { return m_written; }
    uint32_t changes() const { return m_changes; }
    uint32_t guardTrips() const { return m_guardTrips; }
    const Harness::ResonanceConfig& config() const { return m_cfg; }

    /// Natural frequency at `currentA`, Hz (0 without a model)
    float naturalHz(float currentA) const;
    /// The speed (full steps/s) at which harmonic k meets f_n at `currentA`
    float resonantFsps(float currentA, uint8_t k) const;

private:
    bool clearAt(float speed, float currentA, float margin) const;
    bool cutLagTooLarge(const ResonanceInput& in);
    void choose(const ResonanceInput& in, float speed, float band);
    float bandFraction() const;

    Harness::ResonanceConfig m_cfg = {};
    Harness::ResonanceState m_state = Harness::ResonanceState::OFF;
    bool    m_unsupported = false;
    float   m_target = 0.0f;
    float   m_written = 0.0f;
    float   m_lastRunA = 0.0f;
    bool    m_cutLocked = false;     ///< a cut tripped the guard in this band
    bool    m_steadyNow = false;       ///< this call's speed counts as steady (choose() may cut)
    bool    m_chooseSteady = false;    ///< the last search ran at steady speed
    float   m_chooseSpeed = -1.0f;   ///< the speed of the last search (UNAVOIDABLE re-searches on a change)
    float   m_feAvg = 0.0f;
    bool    m_feSeeded = false;
    float   m_feBaseline = 0.0f;       ///< the steady lag in the cut
    bool    m_feBaselineValid = false;
    float   m_preCutFe = 0.0f;         ///< the steady lag before the cut, at the same speed
    bool    m_preCutValid = false;
    float   m_prevSpeed = 0.0f;
    uint16_t m_steady = 0;             ///< consecutive steady calls
    float   m_steadyFe = 0.0f;         ///< the lag at the last steady call, and its speed
    float   m_steadyFeSpeed = -1.0f;
    uint32_t m_changes = 0;
    uint32_t m_guardTrips = 0;
};

} // namespace Services
