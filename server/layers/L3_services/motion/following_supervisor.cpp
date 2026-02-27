/**
 * @file following_supervisor.cpp
 * @brief Following Error Supervisor implementation
 *
 * Pure-logic: no RTOS, no heap, no driver access.
 * All time tracking via accumulated dtSec for deterministic host-side testing.
 */

#include "L3_services/motion/following_supervisor.hpp"

namespace Services {

// Global instance
FollowingSupervisor g_supervisor;

// ── Configuration ──

void FollowingSupervisor::configure(const SupervisorConfig& cfg) {
    m_cfg = cfg;
}

void FollowingSupervisor::configurePID(float kp, float ki, float kd,
                                        float outLimit, float integralLimit) {
    m_pid.setGains(kp, ki, kd);
    m_pid.setOutputLimits(-outLimit, outLimit);
    m_pid.setIntegralLimits(-integralLimit, integralLimit);
}

void FollowingSupervisor::resetPID() {
    m_pid.reset();
}

// ── Motion lifecycle ──

void FollowingSupervisor::beginMove(int32_t setpointVelTps, bool forward,
                                     uint32_t baseSpeedRaw) {
    m_setpointTps  = setpointVelTps;
    m_forward      = forward;
    m_baseSpeedRaw = baseSpeedRaw;
    m_lastRawSpeed = baseSpeedRaw;
    m_state        = SupervisorState::MOVING;

    // Reset error tracking
    m_errorAccumSec = 0.0f;
    m_overThreshold = false;
    m_retryCount    = 0;
    m_needsOffsetCalibration = true;  // Calibrate on first evaluate()
    m_pid.reset();

    // Update telemetry
    m_telem.state    = m_state;
    m_telem.setpoint = m_setpointTps;
}

void FollowingSupervisor::beginHold(int32_t holdPosTicks) {
    m_holdTargetTicks = holdPosTicks;
    m_state           = SupervisorState::HOLDING;

    // Reset error tracking for hold phase
    m_errorAccumSec = 0.0f;
    m_overThreshold = false;
    m_needsOffsetCalibration = true;  // Recalibrate for hold baseline
    m_pid.reset();

    m_telem.state = m_state;
}

void FollowingSupervisor::goIdle() {
    m_state         = SupervisorState::IDLE;
    m_setpointTps   = 0;
    m_baseSpeedRaw  = 0;
    m_errorAccumSec = 0.0f;
    m_overThreshold = false;
    m_retryCount    = 0;
    m_recoveryAccumSec = 0.0f;
    m_pid.reset();

    m_initialOffset = 0;
    m_needsOffsetCalibration = false;

    m_telem = {};
    m_telem.state = SupervisorState::IDLE;
    m_telem.tier  = Tier::OBSERVE;
}

bool FollowingSupervisor::clearFault() {
    if (m_state != SupervisorState::FAULT) {
        return false;
    }
    goIdle();
    return true;
}

// ── Main evaluation ──

SupervisorAction FollowingSupervisor::evaluate(const SupervisorInput& input,
                                                ControlMode mode) {
    // Calibrate initial offset on first evaluate after beginMove/beginHold
    // This captures any pre-existing position mismatch so we only track
    // NEW error that develops during the current motion
    int32_t rawPosError = input.cmdPosTicks - input.encPosTicks;
    if (m_needsOffsetCalibration) {
        m_initialOffset = rawPosError;
        m_needsOffsetCalibration = false;
    }

    // Position error relative to baseline (positive = motor ahead / encoder lagging)
    m_posError = rawPosError - m_initialOffset;
    int32_t absError = absVal(m_posError);

    // Compute velocity error (positive = encoder too slow)
    m_velError = m_setpointTps - input.encVelTps;

    // Update telemetry base fields
    m_telem.posError = m_posError;
    m_telem.velError = m_velError;
    m_telem.state    = m_state;

    // OPEN_LOOP: telemetry only, no control action
    if (mode == ControlMode::OPEN_LOOP) {
        m_telem.tier = Tier::OBSERVE;
        m_telem.pidOutput = 0;
        m_telem.pTerm = 0;
        m_telem.iTerm = 0;
        m_telem.dTerm = 0;
        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    }

    SupervisorAction act;

    switch (m_state) {
        case SupervisorState::IDLE:
            act = makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
            break;

        case SupervisorState::MOVING:
            act = evaluateMoving(input, mode, absError);
            break;

        case SupervisorState::HOLDING:
            act = evaluateHolding(input, absError);
            break;

        case SupervisorState::RECOVERY:
            act = evaluateRecovery(input, absError);
            break;

        case SupervisorState::FAULT:
            act = makeAction(SupervisorAction::Type::NONE, Tier::FAULT_LATCH);
            break;

        default:
            act = makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
            break;
    }

    m_telem.state         = m_state;
    m_telem.tier          = act.tier;
    m_telem.retryCount    = m_retryCount;
    m_telem.errorDurationMs = static_cast<uint32_t>(m_errorAccumSec * 1000.0f);
    return act;
}

// ── MOVING state evaluation ──

SupervisorAction FollowingSupervisor::evaluateMoving(
        const SupervisorInput& input, ControlMode mode, int32_t absError) {

    // Tier 3: hard limit check (immediate fault)
    if (m_cfg.hardLimit > 0 && absError > m_cfg.hardLimit) {
        return makeFaultAction();
    }

    // In SPEED_TRIM mode, the trim controller handles speed correction.
    // Supervisor only monitors for fault escalation — no PID, no SET_SPEED.
    // Uses wider thresholds (4x moveError, 2x moveTime) because trim introduces
    // intentional speed variation, and index pulse causes periodic position spikes.
    if (mode == ControlMode::SPEED_TRIM) {
        m_telem.pidOutput = 0;
        m_telem.pTerm = 0;
        m_telem.iTerm = 0;
        m_telem.dTerm = 0;

        // Index pulse blanking: encoder index causes ~270-tick position spike
        // each revolution. Blank error accumulation for 2 cycles after revolution change.
        if (input.revolutions != m_lastRevolutions) {
            m_lastRevolutions = input.revolutions;
            m_indexBlankCycles = 2;
        }
        if (m_indexBlankCycles > 0) {
            m_indexBlankCycles--;
            m_errorAccumSec = 0.0f;
            m_overThreshold = false;
            return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
        }

        // Wider thresholds for SPEED_TRIM: 4x position error, 2x debounce time
        int32_t effectiveMoveError = m_cfg.moveError * 4;
        uint32_t effectiveMoveTimeMs = m_cfg.moveTimeMs * 2;

        // Position error tracking for escalation
        if (effectiveMoveError > 0 && absError > effectiveMoveError) {
            m_errorAccumSec += input.dtSec;
            m_overThreshold = true;

            float moveTimeSec = static_cast<float>(effectiveMoveTimeMs) / 1000.0f;
            if (m_errorAccumSec > moveTimeSec) {
                return makeRecoveryAction();
            }
        } else {
            m_errorAccumSec = 0.0f;
            m_overThreshold = false;
        }

        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    }

    // MONITOR mode: threshold-based observation and escalation only
    if (m_cfg.moveError > 0 && absError > m_cfg.moveError) {
        m_errorAccumSec += input.dtSec;
        m_overThreshold = true;

        m_telem.pidOutput = 0;
        m_telem.pTerm = 0;
        m_telem.iTerm = 0;
        m_telem.dTerm = 0;

        float moveTimeSec = static_cast<float>(m_cfg.moveTimeMs) / 1000.0f;
        if (m_errorAccumSec > moveTimeSec) {
            return makeRecoveryAction();
        }
        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    } else {
        m_errorAccumSec = 0.0f;
        m_overThreshold = false;

        m_telem.pidOutput = 0;
        m_telem.pTerm = 0;
        m_telem.iTerm = 0;
        m_telem.dTerm = 0;

        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    }
}

// ── HOLDING state evaluation ──

SupervisorAction FollowingSupervisor::evaluateHolding(
        const SupervisorInput& input, int32_t absError) {

    // Tier 3: hard limit (immediate fault)
    if (m_cfg.hardLimit > 0 && absError > m_cfg.hardLimit) {
        return makeFaultAction();
    }

    // Threshold check: E_HOLD
    if (m_cfg.holdError > 0 && absError > m_cfg.holdError) {
        m_errorAccumSec += input.dtSec;
        m_overThreshold = true;

        float holdTimeSec = static_cast<float>(m_cfg.holdTimeMs) / 1000.0f;
        if (m_errorAccumSec > holdTimeSec) {
            // Hold error persisted beyond threshold — fault
            return makeFaultAction();
        }
        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    } else {
        m_errorAccumSec = 0.0f;
        m_overThreshold = false;
        return makeAction(SupervisorAction::Type::NONE, Tier::OBSERVE);
    }
}

// ── RECOVERY state evaluation ──

SupervisorAction FollowingSupervisor::evaluateRecovery(
        const SupervisorInput& input, int32_t absError) {

    m_recoveryAccumSec += input.dtSec;

    // Check if error has cleared (below moveError threshold)
    bool errorCleared = (m_cfg.moveError <= 0) || (absError < m_cfg.moveError);
    if (errorCleared) {
        // Recovery succeeded — return to prior state
        m_state = m_preRecoveryState;
        m_errorAccumSec = 0.0f;
        m_overThreshold = false;
        m_recoveryAccumSec = 0.0f;
        m_pid.reset();

        SupervisorAction act;
        act.type           = SupervisorAction::Type::NONE;
        act.tier           = Tier::OBSERVE;
        act.recoveredEvent = true;
        return act;
    }

    // Check recovery timeout or retry exhaustion
    float recoveryTimeSec = static_cast<float>(
        m_cfg.recoveryTimeMs > 0 ? m_cfg.recoveryTimeMs : 2000) / 1000.0f;
    if (m_recoveryAccumSec > recoveryTimeSec ||
        m_retryCount >= m_cfg.maxRetries) {
        return makeFaultAction();
    }

    // Still in recovery — motor should be stopped already
    return makeAction(SupervisorAction::Type::NONE, Tier::PAUSE_RECOVER);
}

// ── Action helpers ──

SupervisorAction FollowingSupervisor::makeAction(
        SupervisorAction::Type type, Tier tier) const {
    SupervisorAction act;
    act.type      = type;
    act.direction = m_forward;
    act.tier      = tier;
    return act;
}

SupervisorAction FollowingSupervisor::makeFaultAction() {
    m_state = SupervisorState::FAULT;
    m_pid.reset();

    SupervisorAction act;
    act.type       = SupervisorAction::Type::HARD_STOP;
    act.tier       = Tier::FAULT_LATCH;
    act.faultEvent = true;
    return act;
}

SupervisorAction FollowingSupervisor::makeRecoveryAction() {
    m_preRecoveryState = m_state;
    m_state            = SupervisorState::RECOVERY;
    m_retryCount++;
    m_recoveryAccumSec = 0.0f;
    m_pid.reset();

    SupervisorAction act;
    act.type          = SupervisorAction::Type::SOFT_STOP;
    act.tier          = Tier::PAUSE_RECOVER;
    act.recoveryEvent = true;
    return act;
}

// ── PID correction → raw speed ──

uint32_t FollowingSupervisor::applyPIDCorrection(
        const SupervisorInput& input, float pidOutput) {
    // PID output is in encoder ticks/sec.
    // Convert to motor steps/sec adjustment:
    //   adjustSps = pidOutput * fullStepsPerRev / encoderPPR
    float adjustSps = pidOutput
        * static_cast<float>(input.fullStepsPerRev)
        / static_cast<float>(input.encoderPPR);

    // Base speed in steps/sec
    uint32_t baseSps = static_cast<uint32_t>(
        (static_cast<uint64_t>(m_baseSpeedRaw) * 15625ULL) / 1048576ULL);

    // Compute new speed (signed to allow reduction below base)
    int32_t newSps = static_cast<int32_t>(baseSps)
                   + static_cast<int32_t>(adjustSps);

    // Clamp: no reversal, no exceeding MAX_SPEED
    if (newSps < 0) { newSps = 0; }
    uint32_t maxSps = static_cast<uint32_t>(
        (static_cast<uint64_t>(input.maxSpeedRaw) * 15625ULL) / 1024ULL);
    if (static_cast<uint32_t>(newSps) > maxSps) {
        newSps = static_cast<int32_t>(maxSps);
    }

    // Convert back to raw speed register value
    uint32_t newRaw = static_cast<uint32_t>(
        (static_cast<uint64_t>(newSps) * 1048576ULL) / 15625ULL);

    return newRaw;
}

uint32_t FollowingSupervisor::rateLimitSpeed(uint32_t desired,
                                              uint32_t current) const {
    if (m_cfg.maxDeltaVRaw == 0) {
        return desired;  // No rate limiting
    }

    if (desired > current) {
        uint32_t delta = desired - current;
        if (delta > m_cfg.maxDeltaVRaw) {
            return current + m_cfg.maxDeltaVRaw;
        }
    } else {
        uint32_t delta = current - desired;
        if (delta > m_cfg.maxDeltaVRaw) {
            return current - m_cfg.maxDeltaVRaw;
        }
    }
    return desired;
}

// ── Telemetry ──

SupervisorTelemetry FollowingSupervisor::getTelemetry() const {
    return m_telem;
}

} // namespace Services
