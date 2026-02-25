/**
 * @file pid_controller.cpp
 * @brief PID controller implementation
 */

#include "services/pid_controller.hpp"

namespace Services {

void PIDController::setGains(float kp, float ki, float kd) {
    m_kp = kp;
    m_ki = ki;
    m_kd = kd;
}

void PIDController::setOutputLimits(float min, float max) {
    m_outMin = min;
    m_outMax = max;
}

void PIDController::setIntegralLimits(float min, float max) {
    m_iMin = min;
    m_iMax = max;
}

void PIDController::reset() {
    m_integral = 0.0f;
    m_prevError = 0.0f;
    m_firstUpdate = true;
}

PIDController::Output PIDController::update(float error, float dt_sec) {
    Output out = {};

    if (dt_sec <= 0.0f) {
        return out;
    }

    // Proportional
    out.pTerm = m_kp * error;

    // Integral with anti-windup clamping
    m_integral += error * dt_sec;
    if (m_integral > m_iMax) m_integral = m_iMax;
    if (m_integral < m_iMin) m_integral = m_iMin;
    out.iTerm = m_ki * m_integral;

    // Derivative (skip on first call to avoid spike)
    if (m_firstUpdate) {
        out.dTerm = 0.0f;
        m_firstUpdate = false;
    } else {
        float derivative = (error - m_prevError) / dt_sec;
        out.dTerm = m_kd * derivative;
    }
    m_prevError = error;

    // Sum and clamp output
    out.total = out.pTerm + out.iTerm + out.dTerm;
    if (out.total > m_outMax) out.total = m_outMax;
    if (out.total < m_outMin) out.total = m_outMin;

    return out;
}

} // namespace Services
