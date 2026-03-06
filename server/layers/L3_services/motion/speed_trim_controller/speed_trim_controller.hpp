/**
 * @file speed_trim_controller.hpp
 * @brief Speed-trim PI controller for closed-loop velocity correction
 *
 * Architecture: final_speed = base_speed + trim
 *   - PI controller on velocity error (encoder ticks/sec)
 *   - Trim output clamped to percentage of base command
 *   - Integrator gating on bad measurement quality or near-zero command
 *   - No RTOS, no heap, no driver access (pure logic)
 */

#pragma once

#include "harness/pins/velocity_quality.hpp"
#include <stdint.h>

namespace Services {

/**
 * @brief Configuration for speed-trim PI controller
 *
 * Loaded from flash (reinterpreted PID fields).
 * Zero values use defaults.
 */
struct SpeedTrimConfig {
    float kp;               // Proportional gain
    float ki;               // Integral gain
    float outputLimit;      // Max trim output (encoder tps), default 500
    float integralLimit;    // Anti-windup clamp, default = outputLimit
    float deadband;         // Velocity error deadband (tps), default 200
    uint8_t trimMaxPercent; // Max trim as % of base command speed, default 8
};

/**
 * @brief Input snapshot for one trim update cycle
 *
 * Built by motor_task from sensor + command state each 50ms tick.
 */
struct SpeedTrimInput {
    int32_t  encVelTps;         // Measured encoder velocity (ticks/sec, signed)
    VelocityQuality velQuality; // Measurement quality from encoder_task
    bool     forward;           // Motor direction
    uint16_t fullStepsPerRev;   // Mechanical full steps/rev (typically 200)
    uint16_t encoderPPR;        // Encoder pulses/rev (e.g., 4000)
    uint32_t maxSpeedRaw;       // MAX_SPEED register value (10-bit raw)
    float    dtSec;             // Time step (typically 0.050)
};

/**
 * @brief Output of one trim update cycle
 *
 * Contains the final speed to issue and telemetry data.
 */
struct SpeedTrimResult {
    uint32_t finalSpeedRaw;     // base + trim, for s_motor->run()
    int32_t  trimTps;           // Trim in encoder tps (for telemetry)
    int32_t  trimRaw;           // Trim in raw register units (signed)
    float    pTerm;             // Proportional component
    float    iTerm;             // Integral component (after anti-windup)
    bool     active;            // true if trim is running
    bool     frozen;            // true if integrator is frozen this cycle
};

/**
 * @brief Speed-trim PI controller
 *
 * Pure-logic PI controller that computes a speed trim adjustment.
 * Call configure() + beginTrim() to start, update() each 50ms,
 * reset() on stop/mode change.
 */
class SpeedTrimController {
public:
    /**
     * @brief Load configuration (gains, limits)
     */
    void configure(const SpeedTrimConfig& cfg);

    /**
     * @brief Begin trim operation for a new RUN command
     * @param setpointTps  Target velocity in encoder ticks/sec
     * @param forward      Motor direction
     * @param baseSpeedRaw Original commanded speed (raw register)
     */
    void beginTrim(int32_t setpointTps, bool forward, uint32_t baseSpeedRaw);

    /**
     * @brief Reset controller state (integral, active flag)
     *
     * Call on stop, mode change, or fault.
     */
    void reset();

    /**
     * @brief Compute one trim iteration
     * @param input Sensor + command snapshot
     * @return Trim result with final speed and telemetry
     */
    SpeedTrimResult update(const SpeedTrimInput& input);

    /**
     * @brief Check if trim is actively running
     */
    bool isActive() const { return m_active; }

    // Accessors for telemetry/SCPI queries
    float getKp() const { return m_cfg.kp; }
    float getKi() const { return m_cfg.ki; }
    float getOutputLimit() const { return m_cfg.outputLimit; }
    float getIntegralLimit() const { return m_cfg.integralLimit; }
    uint8_t getTrimMaxPercent() const { return m_cfg.trimMaxPercent; }
    float getIntegral() const { return m_integral; }

private:
    SpeedTrimConfig m_cfg = {};

    // PI state
    float    m_integral     = 0.0f;
    bool     m_active       = false;
    bool     m_frozen       = false;
    int32_t  m_setpointTps  = 0;
    bool     m_forward      = true;
    uint32_t m_baseSpeedRaw = 0;

    // Check if integrator should be frozen this cycle
    bool shouldFreeze(const SpeedTrimInput& input) const;
};

// Global instance
extern SpeedTrimController g_speedTrim;

} // namespace Services
