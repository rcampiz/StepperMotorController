/**
 * @file sysid.cpp
 * @brief System Identification Runner implementation
 *
 * Pure-logic: no RTOS, no heap, no driver access.
 * Time tracked via 50ms tick increments from motor_task.
 */

#include "L3_services/motion/sysid.hpp"
#include <math.h>

namespace Services {

// Global instance
SysIdRunner g_sysId;

// ── Conversion helpers ──

uint32_t SysIdRunner::speedSpsToRaw(uint16_t sps) {
    // raw = sps * 1048576 / 15625
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(sps) * 1048576ULL) / 15625ULL);
}

int16_t SysIdRunner::speedSpsToTps(uint16_t sps, uint16_t fpr, uint16_t ppr) {
    // tps = sps * encoderPPR / fullStepsPerRev
    if (fpr == 0) fpr = 200;
    int32_t tps = static_cast<int32_t>(sps) * ppr / fpr;
    return clamp16(tps);
}

int16_t SysIdRunner::clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<int16_t>(v);
}

uint16_t SysIdRunner::interpolateRampSpeed() const {
    // Linear interpolation: startSpeed → targetSpeed over activeDuration
    if (m_cfg.activeDuration_ms == 0) return m_cfg.targetSpeed_sps;

    uint32_t elapsed = m_phaseElapsed_ms;
    uint32_t duration = m_cfg.activeDuration_ms;

    if (elapsed >= duration) return m_cfg.targetSpeed_sps;

    // speed = start + (target - start) * elapsed / duration
    int32_t start = static_cast<int32_t>(m_cfg.startSpeed_sps);
    int32_t target = static_cast<int32_t>(m_cfg.targetSpeed_sps);
    int32_t speed = start + (target - start) * static_cast<int32_t>(elapsed)
                    / static_cast<int32_t>(duration);

    if (speed < 0) speed = 0;
    if (speed > 15625) speed = 15625;  // Max steps/s
    return static_cast<uint16_t>(speed);
}

uint16_t SysIdRunner::generateSineSpeed() const {
    // Sinusoidal: speed = baseline + amplitude * sin(2π * freq * t)
    // baseline = startSpeed, amplitude = targetSpeed (treated as peak deviation)
    float timeS = static_cast<float>(m_phaseElapsed_ms) / 1000.0f;
    float freqHz = static_cast<float>(m_cfg.frequency_mHz) / 1000.0f;
    float baseline = static_cast<float>(m_cfg.startSpeed_sps);
    float amplitude = static_cast<float>(m_cfg.targetSpeed_sps);

    float val = baseline + amplitude * sinf(2.0f * 3.14159265f * freqHz * timeS);

    if (val < 0.0f) val = 0.0f;
    if (val > 15625.0f) val = 15625.0f;
    return static_cast<uint16_t>(val);
}

uint16_t SysIdRunner::generateTrapezoidSpeed() const {
    // 4-phase trapezoid within activeDuration:
    //   25% ramp up → 25% hold → 25% ramp down → 25% hold at zero
    uint32_t dur = m_cfg.activeDuration_ms;
    if (dur == 0) return 0;

    uint32_t elapsed = m_phaseElapsed_ms;
    uint32_t q1 = dur / 4;
    uint32_t q2 = dur / 2;
    uint32_t q3 = (dur * 3) / 4;

    int32_t target = static_cast<int32_t>(m_cfg.targetSpeed_sps);

    if (elapsed < q1) {
        // Ramp up: 0 → target
        return static_cast<uint16_t>((target * elapsed) / q1);
    } else if (elapsed < q2) {
        // Hold at target
        return m_cfg.targetSpeed_sps;
    } else if (elapsed < q3) {
        // Ramp down: target → 0
        uint32_t fallElapsed = elapsed - q2;
        uint32_t fallDuration = q3 - q2;
        return static_cast<uint16_t>(target - (target * fallElapsed) / fallDuration);
    } else {
        // Hold at zero
        return 0;
    }
}

uint16_t SysIdRunner::generateRectangleSpeed() const {
    // Rectangular pulse: first half = target, second half = 0
    uint32_t dur = m_cfg.activeDuration_ms;
    if (dur == 0) return 0;

    if (m_phaseElapsed_ms < dur / 2) {
        return m_cfg.targetSpeed_sps;
    }
    return 0;
}

void SysIdRunner::recordSample(uint16_t time_ms, int16_t setpointTps,
                                int32_t actualTps, int32_t posError) {
    if (m_sampleCount >= SYSID_MAX_SAMPLES) return;

    SysIdSample& s = m_samples[m_sampleCount];
    s.time_ms      = time_ms;
    s.setpoint_tps = setpointTps;
    s.actual_tps   = clamp16(actualTps);
    s.pos_error    = clamp16(posError);
    m_sampleCount++;
}

// ── Start / Abort ──

bool SysIdRunner::start(const SysIdConfig& cfg) {
    // Don't interrupt a running test
    if (m_phase == SysIdPhase::PRE_SETTLE ||
        m_phase == SysIdPhase::ACTIVE ||
        m_phase == SysIdPhase::POST_SETTLE) {
        return false;
    }

    m_cfg = cfg;
    m_phase = SysIdPhase::PRE_SETTLE;
    m_phaseElapsed_ms = 0;
    m_totalElapsed_ms = 0;
    m_sampleCount = 0;
    return true;
}

void SysIdRunner::abort() {
    m_phase = SysIdPhase::IDLE;
    m_phaseElapsed_ms = 0;
    m_totalElapsed_ms = 0;
}

// ── Main tick (called every 50ms) ──

SysIdRunner::TickResult SysIdRunner::tick(
        int32_t encVelTps, int32_t posError,
        uint16_t fullStepsPerRev, uint16_t encoderPPR) {

    TickResult result{};
    result.rawSpeed = 0;
    result.forward = m_cfg.forward;
    result.active = false;
    result.justFinished = false;

    switch (m_phase) {
        case SysIdPhase::IDLE:
        case SysIdPhase::DONE:
            // Not active — motor_task handles motor normally
            return result;

        case SysIdPhase::PRE_SETTLE:
            result.active = true;
            result.rawSpeed = 0;  // Motor stopped during settle

            // Record baseline samples (setpoint = 0)
            recordSample(m_totalElapsed_ms, 0, encVelTps, posError);

            m_phaseElapsed_ms += 50;
            m_totalElapsed_ms += 50;
            if (m_phaseElapsed_ms >= m_cfg.settleDuration_ms) {
                m_phase = SysIdPhase::ACTIVE;
                m_phaseElapsed_ms = 0;  // Reset phase timer, total keeps going
            }
            return result;

        case SysIdPhase::ACTIVE: {
            result.active = true;

            // Determine current speed based on test type
            uint16_t currentSpeed_sps;
            switch (m_cfg.type) {
                case SysIdTestType::STEP:
                    currentSpeed_sps = m_cfg.targetSpeed_sps;
                    break;
                case SysIdTestType::RAMP:
                    currentSpeed_sps = interpolateRampSpeed();
                    break;
                case SysIdTestType::SINE:
                    currentSpeed_sps = generateSineSpeed();
                    break;
                case SysIdTestType::TRAPEZOID:
                    currentSpeed_sps = generateTrapezoidSpeed();
                    break;
                case SysIdTestType::RECTANGLE:
                    currentSpeed_sps = generateRectangleSpeed();
                    break;
                default:
                    currentSpeed_sps = 0;
                    break;
            }

            result.rawSpeed = speedSpsToRaw(currentSpeed_sps);
            result.forward = m_cfg.forward;

            // Compute setpoint in encoder ticks/sec for logging
            int16_t setpointTps = speedSpsToTps(
                currentSpeed_sps, fullStepsPerRev, encoderPPR);
            if (!m_cfg.forward) {
                setpointTps = static_cast<int16_t>(-setpointTps);
            }

            // Record sample with monotonic timestamp
            recordSample(m_totalElapsed_ms, setpointTps, encVelTps, posError);

            m_phaseElapsed_ms += 50;
            m_totalElapsed_ms += 50;

            // Check termination: duration expired or buffer full
            if (m_phaseElapsed_ms >= m_cfg.activeDuration_ms ||
                m_sampleCount >= SYSID_MAX_SAMPLES) {
                m_phase = SysIdPhase::POST_SETTLE;
                m_phaseElapsed_ms = 0;
            }
            return result;
        }

        case SysIdPhase::POST_SETTLE:
            result.active = true;
            result.rawSpeed = 0;  // Motor stopping

            // Record deceleration samples (setpoint = 0)
            recordSample(m_totalElapsed_ms, 0, encVelTps, posError);

            m_phaseElapsed_ms += 50;
            m_totalElapsed_ms += 50;
            if (m_phaseElapsed_ms >= m_cfg.settleDuration_ms) {
                m_phase = SysIdPhase::DONE;
                m_phaseElapsed_ms = 0;
                result.justFinished = true;
            }
            return result;
    }

    return result;
}

} // namespace Services
