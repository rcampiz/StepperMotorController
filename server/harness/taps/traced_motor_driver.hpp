/**
 * @file traced_motor_driver.hpp
 * @brief Traced wrapper for IMotorDriver (L3->L4 boundary)
 *
 * Logs motor commands and status queries via ITrace. Pure observer: it never writes
 * telemetry (MotorController::periodicUpdate owns the snapshot writes).
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "L4_drivers/boundary/imotor_driver.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

#include <stdio.h>  // snprintf for detail formatting

namespace Harness {

class TracedMotorDriver : public Drivers::IMotorDriver {
public:
    explicit TracedMotorDriver(Drivers::IMotorDriver& real) : m_real(real) {}

    void init() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "motor.init");
        m_real.init();
    }

    void run(bool forward, float fullStepsPerSec) override {
        char detail[24];
        snprintf(detail, sizeof(detail), "%s,%s", fsps(fullStepsPerSec).s, forward ? "fwd" : "rev");
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "run", detail);
        m_real.run(forward, fullStepsPerSec);
    }

    void move(bool forward, uint32_t microsteps) override {
        char detail[24];
        snprintf(detail, sizeof(detail), "%lu,%s",
                 (unsigned long)microsteps, forward ? "fwd" : "rev");
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "move", detail);
        m_real.move(forward, microsteps);
    }

    void goTo(int32_t positionMicrosteps) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%ld", (long)positionMicrosteps);
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "goTo", detail);
        m_real.goTo(positionMicrosteps);
    }

    void setMaxSpeed(float fullStepsPerSec) override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "setMaxSpeed", fsps(fullStepsPerSec).s);
        m_real.setMaxSpeed(fullStepsPerSec);
    }

    float getMaxSpeed() override {
        const float v = m_real.getMaxSpeed();
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3<L4]", "getMaxSpeed", fsps(v).s);
        return v;
    }

    Drivers::MotorStatus readState() override {
        Drivers::MotorStatus st = m_real.readState();
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_MOTOR, "[L3<L4]", "readState",
                          static_cast<uint32_t>(st.raw));
        return st;
    }

    void softStop() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "softStop");
        m_real.softStop();
    }

    void hardStop() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "hardStop");
        m_real.hardStop();
    }

    void softHiZ() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "softHiZ");
        m_real.softHiZ();
    }

    void hardHiZ() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "hardHiZ");
        m_real.hardHiZ();
    }

    void goHome() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "goHome");
        m_real.goHome();
    }

    void goMark() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "goMark");
        m_real.goMark();
    }

    void resetPos() override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "resetPos");
        m_real.resetPos();
    }

    void setMark(int32_t positionMicrosteps) override {
        char detail[16];
        snprintf(detail, sizeof(detail), "%ld", (long)positionMicrosteps);
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "setMark", detail);
        m_real.setMark(positionMicrosteps);
    }

    void setAcceleration(float fsps2) override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "setAcceleration", fsps(fsps2).s);
        m_real.setAcceleration(fsps2);
    }

    void setDeceleration(float fsps2) override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "setDeceleration", fsps(fsps2).s);
        m_real.setDeceleration(fsps2);
    }

    // Pure calls (no bus traffic, any task): forwarded untraced
    const Drivers::DriverCaps& caps() const override { return m_real.caps(); }
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override { return m_real.quantize(s); }
    bool toLegacyUnits(const Drivers::DriverSettings& s, Drivers::LegacyDriverUnits& out) const override {
        return m_real.toLegacyUnits(s, out);
    }
    bool fromLegacyUnits(const Drivers::LegacyDriverUnits& in, Drivers::DriverSettings& s) const override {
        return m_real.fromLegacyUnits(in, s);
    }

    Drivers::ApplyResult apply(const Drivers::DriverSettings& s) override {
        const Drivers::ApplyResult r = m_real.apply(s);
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_MOTOR, "[L3<L4]", "apply", static_cast<uint32_t>(r));
        return r;
    }

    bool readSettings(Drivers::DriverSettings& inForce) override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "readSettings");
        return m_real.readSettings(inForce);
    }

    bool checkReset() override {
        return m_real.checkReset();   // every 500 ms, too noisy
    }

    bool setRunCurrentLive(float amps) override {
        const bool ok = m_real.setRunCurrentLive(amps);
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_MOTOR, "[L3<L4]", "setRunCurrentLive",
                                   static_cast<uint32_t>(amps * 1000.0f));
        return ok;
    }

    size_t diagnostics(Harness::DriverDiagView view, char* buf, size_t size) override {
        Harness::ITrace::log(Harness::ITrace::L3_L4_MOTOR, "[L3>L4]", "diagnostics");
        return m_real.diagnostics(view, buf, size);
    }

    Drivers::MotorStatus getStatus() override {
        Drivers::MotorStatus st = m_real.getStatus();
        Harness::ITrace::logResult(Harness::ITrace::L3_L4_MOTOR, "[L3<L4]", "getStatus",
                          static_cast<uint32_t>(st.raw));
        return st;
    }

    bool isBusy() override {
        return m_real.isBusy();  // called every 50ms, too noisy
    }

    bool flagActive() override {
        return m_real.flagActive();
    }

    void setStandby(bool standby) override {
        m_real.setStandby(standby);   // ISR-safe GPIO write (out-of-band E-STOP): never traced
    }

private:
    Drivers::IMotorDriver& m_real;

    /// "123.45" full steps/s without printf float support (newlib-nano)
    struct Text { char s[16]; };
    static Text fsps(float v) {
        Text t;
        const unsigned long centi = (v > 0.0f) ? static_cast<unsigned long>(v * 100.0f + 0.5f) : 0UL;
        snprintf(t.s, sizeof(t.s), "%lu.%02lu", centi / 100UL, centi % 100UL);
        return t;
    }
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
