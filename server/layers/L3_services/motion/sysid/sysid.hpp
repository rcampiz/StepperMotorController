/**
 * @file sysid.hpp
 * @brief System Identification Runner — automated step/ramp tests for PID tuning
 *
 * Pure-logic class: no RTOS, no heap, no driver access.
 * Motor_task calls tick() every 50ms and applies the returned speed command.
 *
 * Test types:
 *   STEP:      Jump to target speed, record response
 *   RAMP:      Linearly sweep from start to target speed
 *   SINE:      Sinusoidal oscillation around baseline speed
 *   TRAPEZOID: Ramp-up, hold, ramp-down, hold profile
 *   RECTANGLE: Instant on/off rectangular pulse
 *
 * State machine:
 *   IDLE → PRE_SETTLE → ACTIVE → POST_SETTLE → DONE
 */

#pragma once

#include <stdint.h>

namespace Services {

enum class SysIdTestType : uint8_t {
    STEP      = 0,
    RAMP      = 1,
    SINE      = 2,   // Sinusoidal oscillation at fixed frequency
    TRAPEZOID = 3,   // Trapezoidal profile (ramp-up, hold, ramp-down, hold)
    RECTANGLE = 4    // Rectangular pulse (on, hold, off, hold)
};

enum class SysIdPhase : uint8_t {
    IDLE        = 0,
    PRE_SETTLE  = 1,   // Motor stopped, waiting for settle
    ACTIVE      = 2,   // Test running, logging data
    POST_SETTLE = 3,   // Motor stopped after test, logging decel
    DONE        = 4    // Data ready for download
};

struct SysIdSample {
    uint16_t time_ms;       // ms since ACTIVE phase start
    int16_t  setpoint_tps;  // commanded encoder velocity (ticks/sec)
    int16_t  actual_tps;    // measured encoder velocity (ticks/sec)
    int16_t  pos_error;     // following error (encoder ticks)
};  // 8 bytes per sample

struct SysIdConfig {
    SysIdTestType type;
    uint16_t targetSpeed_sps;    // Step/Rect: target speed | Ramp: end speed (steps/s)
    uint16_t startSpeed_sps;     // Ramp: starting speed | Sine: baseline speed
    bool     forward;
    uint16_t settleDuration_ms;  // Pre/post settle time (default 1000)
    uint16_t activeDuration_ms;  // Active recording time (default 5000)
    uint16_t frequency_mHz;      // Sine: frequency in millihertz (e.g. 500 = 0.5 Hz)
};

constexpr uint16_t SYSID_MAX_SAMPLES = 400;  // 400 × 8 = 3200 bytes

class SysIdRunner {
public:
    /**
     * @brief Start a test with the given configuration.
     * @return false if a test is already running (ACTIVE or PRE/POST_SETTLE)
     */
    bool start(const SysIdConfig& cfg);

    /**
     * @brief Abort any running test. Motor_task must also stop the motor.
     */
    void abort();

    /**
     * @brief Result from each 50ms tick.
     */
    struct TickResult {
        uint32_t rawSpeed;      // Raw speed register value to command
        bool     forward;       // Direction
        bool     active;        // true = sysid is controlling motor
        bool     justFinished;  // true on the tick that test completes
    };

    /**
     * @brief Called from motor_task each 50ms cycle.
     * @param encVelTps     Measured encoder velocity (ticks/sec)
     * @param posError      Current following error (encoder ticks)
     * @param fullStepsPerRev Motor full steps per revolution
     * @param encoderPPR    Encoder pulses per revolution
     * @return Speed command and status flags
     */
    TickResult tick(int32_t encVelTps, int32_t posError,
                    uint16_t fullStepsPerRev, uint16_t encoderPPR);

    // Accessors
    SysIdPhase getPhase() const { return m_phase; }
    uint16_t getSampleCount() const { return m_sampleCount; }
    const SysIdSample* getSamples() const { return m_samples; }
    const SysIdConfig& getConfig() const { return m_cfg; }

private:
    SysIdConfig  m_cfg{};
    SysIdPhase   m_phase = SysIdPhase::IDLE;
    uint16_t     m_phaseElapsed_ms = 0;   // Time in current phase
    uint16_t     m_totalElapsed_ms = 0;   // Monotonic time since test start
    uint16_t     m_sampleCount = 0;

    SysIdSample  m_samples[SYSID_MAX_SAMPLES];

    // Convert motor steps/sec to raw SPEED register value
    // raw = sps * 1048576 / 15625
    static uint32_t speedSpsToRaw(uint16_t sps);

    // Convert motor steps/sec to encoder ticks/sec
    // tps = sps * encoderPPR / fullStepsPerRev
    static int16_t speedSpsToTps(uint16_t sps, uint16_t fpr, uint16_t ppr);

    // Linearly interpolate ramp speed at given elapsed time
    uint16_t interpolateRampSpeed() const;

    // Generate sinusoidal speed at current elapsed time
    uint16_t generateSineSpeed() const;

    // Generate trapezoidal profile speed at current elapsed time
    uint16_t generateTrapezoidSpeed() const;

    // Generate rectangular pulse speed at current elapsed time
    uint16_t generateRectangleSpeed() const;

    // Clamp int32_t to int16_t range
    static int16_t clamp16(int32_t v);

    // Record one sample
    void recordSample(uint16_t time_ms, int16_t setpointTps,
                      int32_t actualTps, int32_t posError);
};

extern SysIdRunner g_sysId;

} // namespace Services
