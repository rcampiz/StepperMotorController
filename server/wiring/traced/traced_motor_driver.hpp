/**
 * @file traced_motor_driver.hpp
 * @brief Traced wrapper for IMotorDriver (L3->L4 boundary)
 *
 * Logs motor commands and status queries via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "F_platform/hal/imotor_driver.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

#include <stdio.h>  // snprintf for detail formatting

class TracedMotorDriver : public Services::IMotorDriver {
public:
    explicit TracedMotorDriver(Services::IMotorDriver& real) : m_real(real) {}

    void init() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "motor.init");
        m_real.init();
    }

    void run(bool forward, uint32_t speed) override {
        char detail[24];
        snprintf(detail, sizeof(detail), "%lu,%s",
                 (unsigned long)speed, forward ? "fwd" : "rev");
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "run", detail);
        m_real.run(forward, speed);
    }

    void move(bool forward, uint32_t steps) override {
        char detail[24];
        snprintf(detail, sizeof(detail), "%lu,%s",
                 (unsigned long)steps, forward ? "fwd" : "rev");
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "move", detail);
        m_real.move(forward, steps);
    }

    void goTo(int32_t position) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%ld", (long)position);
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "goTo", detail);
        m_real.goTo(position);
    }

    void softStop() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "softStop");
        m_real.softStop();
    }

    void hardStop() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "hardStop");
        m_real.hardStop();
    }

    void softHiZ() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "softHiZ");
        m_real.softHiZ();
    }

    void hardHiZ() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "hardHiZ");
        m_real.hardHiZ();
    }

    void goHome() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "goHome");
        m_real.goHome();
    }

    void goMark() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "goMark");
        m_real.goMark();
    }

    void resetPos() override {
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "resetPos");
        m_real.resetPos();
    }

    uint32_t getParam(uint8_t reg) override {
        uint32_t val = m_real.getParam(reg);
        ITrace::logResult(ITrace::L3_L4_MOTOR, "[L3<L4]", "getParam", val);
        return val;
    }

    void setParam(uint8_t reg, uint32_t value) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "r%u=%lu",
                 (unsigned)reg, (unsigned long)value);
        ITrace::log(ITrace::L3_L4_MOTOR, "[L3>L4]", "setParam", detail);
        m_real.setParam(reg, value);
    }

    Services::MotorStatus getStatus() override {
        Services::MotorStatus st = m_real.getStatus();
        ITrace::logResult(ITrace::L3_L4_MOTOR, "[L3<L4]", "getStatus",
                          static_cast<uint32_t>(st.raw));
        return st;
    }

    bool isBusy() override {
        return m_real.isBusy();  // called every 50ms, too noisy
    }

    bool flagActive() override {
        return m_real.flagActive();
    }

private:
    Services::IMotorDriver& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
