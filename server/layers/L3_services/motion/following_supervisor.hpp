/**
 * @file following_supervisor.hpp
 * @brief Following Error Supervisor — closed-loop slip detection and response
 *
 * Pure-logic class: no RTOS, no heap, no driver access.
 * Motor_task feeds sensor data every 50ms and applies the returned Action.
 *
 * Control modes:
 *   OPEN_LOOP:            Telemetry only, no correction or fault
 *   CLOSED_LOOP_MONITOR:  Detect slip → Tier 2/3 (stop/fault), no correction
 *   SPEED_TRIM:           Monitor-only (trim controller handles correction)
 *
 * Response ladder:
 *   Tier 0 (OBSERVE):       Error within tolerance
 *   Tier 1 (SOFT_CORRECT):  Reserved (unused in current modes)
 *   Tier 2 (PAUSE_RECOVER): Stop motor, attempt recovery
 *   Tier 3 (FAULT_LATCH):   Latched fault, requires explicit clear
 *
 * State machine:
 *   IDLE → MOVING → HOLDING → RECOVERY → FAULT
 */

#pragma once

#include "L3_services/motion/pid_controller.hpp"
#include "L3_services/motion/control_mode.hpp"
#include <stdint.h>

namespace Services {

// ── Supervisor state machine ──
enum class SupervisorState : uint8_t {
    IDLE     = 0,
    MOVING   = 1,
    HOLDING  = 2,
    RECOVERY = 3,
    FAULT    = 4
};

// ── Response tier ──
enum class Tier : uint8_t {
    OBSERVE       = 0,
    SOFT_CORRECT  = 1,
    PAUSE_RECOVER = 2,
    FAULT_LATCH   = 3
};

// ── Threshold configuration ──
struct SupervisorConfig {
    // Motion thresholds
    int32_t  moveError;       // E_MOVE: position error threshold (encoder ticks)
    uint32_t moveTimeMs;      // T_MOVE: debounce time before Tier 2 (ms)
    // Hold thresholds
    int32_t  holdError;       // E_HOLD: position error threshold during hold
    uint32_t holdTimeMs;      // T_HOLD: debounce time for hold error (ms)
    // Hard limit
    int32_t  hardLimit;       // E_HARD: immediate fault threshold (any state)
    // Recovery
    uint8_t  maxRetries;      // N_RETRY: max Tier 2 attempts before Tier 3
    // PID rate limiter
    uint32_t maxDeltaVRaw;    // Max speed change per 50ms cycle (raw units)
    // Recovery timeout
    uint32_t recoveryTimeMs;  // Max time in RECOVERY before fault (ms)
};

// ── Sensor input (filled by motor_task each cycle) ──
struct SupervisorInput {
    int32_t  cmdPosTicks;     // Motor ABS_POS converted to encoder ticks
    int32_t  encPosTicks;     // Encoder count
    int32_t  encVelTps;       // Encoder velocity (ticks/sec)
    uint32_t baseSpeedRaw;    // Original commanded raw speed
    bool     motorBusy;       // powerSTEP01 BUSY status
    bool     forward;         // Commanded direction
    uint32_t maxSpeedRaw;     // MAX_SPEED register value (clamping)
    uint16_t fullStepsPerRev; // Motor config
    uint16_t encoderPPR;      // Encoder config
    float    dtSec;           // Time step (nominally 0.050)
    int32_t  revolutions;     // Encoder revolution count (for index pulse blanking)
};

// ── Action output from evaluate() ──
struct SupervisorAction {
    enum class Type : uint8_t {
        NONE      = 0,
        SET_SPEED = 1,
        SOFT_STOP = 2,
        HARD_STOP = 3
    };

    Type     type           = Type::NONE;
    bool     direction      = true;
    uint32_t rawSpeed       = 0;       // Only valid for SET_SPEED
    Tier     tier           = Tier::OBSERVE;
    bool     faultEvent     = false;   // Transition INTO FAULT
    bool     recoveryEvent  = false;   // Transition INTO RECOVERY
    bool     recoveredEvent = false;   // Recovered from RECOVERY
};

// ── Telemetry snapshot ──
struct SupervisorTelemetry {
    SupervisorState state;
    Tier     tier;
    int32_t  posError;
    int32_t  velError;
    int32_t  setpoint;          // Commanded velocity (ticks/sec)
    int16_t  pidOutput;
    int16_t  pTerm;
    int16_t  iTerm;
    int16_t  dTerm;
    uint8_t  retryCount;
    uint32_t errorDurationMs;
};

// ── The supervisor class ──
class FollowingSupervisor {
public:
    void configure(const SupervisorConfig& cfg);

    void configurePID(float kp, float ki, float kd,
                      float outLimit, float integralLimit);
    void resetPID();

    // Motion lifecycle (called by motor_task on command dispatch)
    void beginMove(int32_t setpointVelTps, bool forward, uint32_t baseSpeedRaw);
    void beginHold(int32_t holdPosTicks);
    void goIdle();

    // Main evaluation — called every 50ms from motor_task
    SupervisorAction evaluate(const SupervisorInput& input, ControlMode mode);

    // Fault management
    bool hasFault() const { return m_state == SupervisorState::FAULT; }
    bool clearFault();

    // Telemetry
    SupervisorTelemetry getTelemetry() const;
    SupervisorState getState() const { return m_state; }
    const PIDController& getPID() const { return m_pid; }

private:
    SupervisorConfig m_cfg{};
    PIDController    m_pid;
    SupervisorState  m_state = SupervisorState::IDLE;

    // Motion context
    int32_t  m_setpointTps   = 0;
    bool     m_forward       = true;
    uint32_t m_baseSpeedRaw  = 0;
    int32_t  m_holdTargetTicks = 0;

    // Error tracking
    int32_t  m_posError      = 0;
    int32_t  m_velError      = 0;
    float    m_errorAccumSec = 0.0f;   // Time over threshold
    bool     m_overThreshold = false;

    // Initial offset calibration: captures position mismatch at motion start
    // so we only track NEW error that develops during the current motion
    int32_t  m_initialOffset = 0;
    bool     m_needsOffsetCalibration = false;

    // Index pulse blanking: skip error accumulation for a few cycles after
    // revolution change to avoid false faults from encoder index pulse jitter
    int32_t  m_lastRevolutions   = 0;
    uint8_t  m_indexBlankCycles  = 0;

    // Recovery
    uint8_t  m_retryCount       = 0;
    float    m_recoveryAccumSec = 0.0f;
    SupervisorState m_preRecoveryState = SupervisorState::MOVING;

    // Rate limiter
    uint32_t m_lastRawSpeed  = 0;

    // Telemetry cache
    SupervisorTelemetry m_telem{};

    // Internal helpers
    SupervisorAction evaluateMoving(const SupervisorInput& input,
                                    ControlMode mode, int32_t absError);
    SupervisorAction evaluateHolding(const SupervisorInput& input,
                                     int32_t absError);
    SupervisorAction evaluateRecovery(const SupervisorInput& input,
                                      int32_t absError);

    SupervisorAction makeAction(SupervisorAction::Type type, Tier tier) const;
    SupervisorAction makeFaultAction();
    SupervisorAction makeRecoveryAction();

    uint32_t applyPIDCorrection(const SupervisorInput& input, float pidOutput);
    uint32_t rateLimitSpeed(uint32_t desired, uint32_t current) const;

    static int32_t absVal(int32_t v) { return v < 0 ? -v : v; }
    static float clampF(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
    static int16_t clamp16(float v) {
        if (v > 32767.0f) return 32767;
        if (v < -32768.0f) return -32768;
        return static_cast<int16_t>(v);
    }
};

// Global instance
extern FollowingSupervisor g_supervisor;

} // namespace Services
