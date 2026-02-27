/**
 * @file pid_controller.hpp
 * @brief Lightweight PID controller for closed-loop motor control
 *
 * Runs in motor_task's 50ms loop (20 Hz). Features:
 *   - Anti-windup (integral clamping)
 *   - Output clamping
 *   - Derivative filtering (first-order)
 *   - Works in encoder ticks/sec for maximum resolution
 */

#pragma once

namespace Services {

class PIDController {
public:
    struct Output {
        float total;    // Clamped PID output
        float pTerm;    // Proportional component
        float iTerm;    // Integral component (after anti-windup)
        float dTerm;    // Derivative component (filtered)
    };

    /**
     * @brief Set PID gains
     * @param kp Proportional gain
     * @param ki Integral gain
     * @param kd Derivative gain
     */
    void setGains(float kp, float ki, float kd);

    /**
     * @brief Set output clamping limits
     * @param min Minimum output (typically negative)
     * @param max Maximum output
     */
    void setOutputLimits(float min, float max);

    /**
     * @brief Set integral term clamping (anti-windup)
     * @param min Minimum integral accumulator
     * @param max Maximum integral accumulator
     */
    void setIntegralLimits(float min, float max);

    /**
     * @brief Reset controller state (integral, previous error)
     *
     * Call when starting a new motion or switching modes.
     */
    void reset();

    /**
     * @brief Compute one PID iteration
     * @param error Setpoint minus measurement (positive = too slow)
     * @param dt_sec Time step in seconds (typically 0.050)
     * @return Output struct with total and individual terms
     */
    Output update(float error, float dt_sec);

    // Accessors
    float getKp() const { return m_kp; }
    float getKi() const { return m_ki; }
    float getKd() const { return m_kd; }
    float getOutputMax() const { return m_outMax; }
    float getIntegralMax() const { return m_iMax; }

private:
    float m_kp = 0.0f;
    float m_ki = 0.0f;
    float m_kd = 0.0f;

    float m_integral = 0.0f;
    float m_prevError = 0.0f;
    bool  m_firstUpdate = true;

    float m_outMin = -10000.0f;
    float m_outMax =  10000.0f;
    float m_iMin   = -10000.0f;
    float m_iMax   =  10000.0f;
};

} // namespace Services
