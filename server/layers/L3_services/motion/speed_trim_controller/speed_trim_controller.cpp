/**
 * @file speed_trim_controller.cpp
 * @brief Speed-trim PI controller implementation
 *
 * PI on velocity error (encoder tps), output converted to raw speed adjustment.
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
                                     uint32_t baseSpeedRaw) {
    m_setpointTps  = setpointTps;
    m_forward      = forward;
    m_baseSpeedRaw = baseSpeedRaw;
    m_integral     = 0.0f;
    m_frozen       = false;
    m_active       = true;
}

void SpeedTrimController::reset() {
    m_integral     = 0.0f;
    m_frozen       = false;
    m_active       = false;
    m_setpointTps  = 0;
    m_baseSpeedRaw = 0;
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
    result.finalSpeedRaw = m_baseSpeedRaw;  // Default: no trim (use stored base)

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
    float baseSpsF = static_cast<float>(m_baseSpeedRaw) * 15625.0f / 1048576.0f;

    float maxTrimSps = baseSpsF
                     * static_cast<float>(m_cfg.trimMaxPercent) / 100.0f;

    // Enforce minimum cap of 1 sps so tiny base speeds still allow some trim
    if (maxTrimSps < 1.0f) maxTrimSps = 1.0f;

    if (trimSps > maxTrimSps)  trimSps = maxTrimSps;
    if (trimSps < -maxTrimSps) trimSps = -maxTrimSps;

    // 7. Compute final speed: base + trim (float), clamp to [0, maxSps]
    float finalSpsF = baseSpsF + trimSps;
    if (finalSpsF < 0.0f) finalSpsF = 0.0f;

    // MAX_SPEED register uses different conversion: sps = raw * 15625 / 1024
    float maxSpsF = static_cast<float>(input.maxSpeedRaw) * 15625.0f / 1024.0f;
    if (finalSpsF > maxSpsF) {
        finalSpsF = maxSpsF;
    }

    // 8. Convert back to raw SPEED register with rounding (not truncation)
    auto finalRaw = static_cast<uint32_t>(
        (finalSpsF * 1048576.0f / 15625.0f) + 0.5f);

    // Compute trim in raw units for telemetry (use stored base, not input)
    auto trimRaw = static_cast<int32_t>(finalRaw) - static_cast<int32_t>(m_baseSpeedRaw);

    result.finalSpeedRaw = finalRaw;
    result.trimRaw = trimRaw;

    return result;
}

} // namespace Services
