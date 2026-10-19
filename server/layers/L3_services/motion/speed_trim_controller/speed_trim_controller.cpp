/**
 * @file speed_trim_controller.cpp
 * @brief Speed-trim PI controller implementation
 *
 * PI on velocity error (encoder tps), output converted to a speed adjustment in full steps/s.
 * Pure logic — no RTOS, no hardware, no heap.
 */

#include "L3_services/motion/speed_trim_controller/speed_trim_controller.hpp"

namespace Services {

// Global instance
SpeedTrimController g_speedTrim;

// ── Configuration ──

void SpeedTrimController::configure(const SpeedTrimConfig& cfg) {
    m_cfg = cfg;

    // Apply defaults for zero values
    if (m_cfg.outputLimit <= 0.0f)  m_cfg.outputLimit = 500.0f;
    if (m_cfg.integralLimit <= 0.0f) m_cfg.integralLimit = m_cfg.outputLimit;
    if (m_cfg.deadband <= 0.0f)     m_cfg.deadband = 200.0f;
    if (m_cfg.trimMaxPercent == 0)   m_cfg.trimMaxPercent = 8;
    if (m_cfg.trimMaxPercent > 50)   m_cfg.trimMaxPercent = 50;
}

// ── Lifecycle ──

void SpeedTrimController::beginTrim(int32_t setpointTps, bool forward,
                                     float baseSpeedFsps) {
    m_setpointTps  = setpointTps;
    m_forward      = forward;
    m_baseSpeedFsps = baseSpeedFsps;
    m_integral     = 0.0f;
    m_frozen       = false;
    m_active       = true;
}

void SpeedTrimController::reset() {
    m_integral     = 0.0f;
    m_frozen       = false;
    m_active       = false;
    m_setpointTps  = 0;
    m_baseSpeedFsps = 0.0f;
}

// ── Integrator freeze logic ──

bool SpeedTrimController::shouldFreeze(const SpeedTrimInput& input) const {
    // Bad measurement — don't accumulate error
    if (input.velQuality == VelocityQuality::STALE ||
        input.velQuality == VelocityQuality::INVALID) {
        return true;
    }

    // Near-zero command — prevent windup at rest
    if (m_setpointTps > -50 && m_setpointTps < 50) {
        return true;
    }

    return false;
}

// ── PI update ──

SpeedTrimResult SpeedTrimController::update(const SpeedTrimInput& input) {
    SpeedTrimResult result = {};
    result.active = m_active;
    result.baseSpeedFsps = m_baseSpeedFsps;
    result.finalSpeedFsps = m_baseSpeedFsps;  // Default: no trim (use stored base)

    if (!m_active || input.dtSec <= 0.0f) {
        return result;
    }

    // Gains of zero = trim disabled, just pass through base speed
    if (m_cfg.kp <= 0.0f && m_cfg.ki <= 0.0f) {
        return result;
    }

    // 1. Velocity error using stored setpoint (positive = motor too slow)
    float velError = static_cast<float>(m_setpointTps - input.encVelTps);

    // 2. Check freeze conditions
    bool freeze = shouldFreeze(input);
    result.frozen = freeze;
    m_frozen = freeze;

    // 3. Integral accumulation (hold in place when frozen, don't zero)
    //    Deadband: don't accumulate when error is small (noise-dominated)
    if (!freeze) {
        float absError = velError < 0.0f ? -velError : velError;
        if (absError >= m_cfg.deadband) {
            m_integral += velError * input.dtSec;

            // Anti-windup clamp
            if (m_integral > m_cfg.integralLimit)  m_integral = m_cfg.integralLimit;
            if (m_integral < -m_cfg.integralLimit) m_integral = -m_cfg.integralLimit;
        }
    }

    // 4. PI output in encoder tps
    float pTerm = m_cfg.kp * velError;
    float iTerm = m_cfg.ki * m_integral;
    float trimTps = pTerm + iTerm;

    // Clamp to output limit (in tps)
    if (trimTps > m_cfg.outputLimit)  trimTps = m_cfg.outputLimit;
    if (trimTps < -m_cfg.outputLimit) trimTps = -m_cfg.outputLimit;

    result.pTerm = pTerm;
    result.iTerm = iTerm;
    result.trimTps = static_cast<int32_t>(trimTps);

    // 5. Convert tps → sps: trimSps = trimTps * fullStepsPerRev / encoderPPR
    float fpr = static_cast<float>(input.fullStepsPerRev);
    float ppr = static_cast<float>(input.encoderPPR);
    if (ppr <= 0.0f) ppr = 4000.0f;  // Safety fallback

    float trimSps = trimTps * fpr / ppr;

    // 6. Apply percentage cap relative to base command speed (stored from beginTrim)
    //    Use float to avoid integer truncation that loses ~1 step/s per cycle
    float baseSpsF = m_baseSpeedFsps;

    float maxTrimSps = baseSpsF
                     * static_cast<float>(m_cfg.trimMaxPercent) / 100.0f;

    // Enforce minimum cap of 1 sps so tiny base speeds still allow some trim
    if (maxTrimSps < 1.0f) maxTrimSps = 1.0f;

    if (trimSps > maxTrimSps)  trimSps = maxTrimSps;
    if (trimSps < -maxTrimSps) trimSps = -maxTrimSps;

    // 7. Compute final speed: base + trim (float), clamp to [0, maxSps]
    float finalSpsF = baseSpsF + trimSps;
    if (finalSpsF < 0.0f) finalSpsF = 0.0f;

    // The driver's speed ceiling (MAX_SPEED on the powerSTEP01)
    if (finalSpsF > input.maxSpeedFsps) {
        finalSpsF = input.maxSpeedFsps;
    }

    // 8. The driver quantises to its chip's resolution (powerSTEP01: 0.0149 full steps/s)
    result.finalSpeedFsps = finalSpsF;

    return result;
}

} // namespace Services
