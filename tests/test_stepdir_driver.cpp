/**
 * @file test_stepdir_driver.cpp
 * @brief Driver boundary step 5: the generic STEP/DIR driver (L4) on fake Board:: pins and a
 *        test-driven step pulse timer. Caps, quantize, the mode pins, the ramps, the pulse
 *        count, reversals, E-STOP, and what it reports instead of silently ignoring.
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "L4_drivers/devices/stepdir_driver.hpp"
#include <cmath>
#include <cstring>
#include <string>

using namespace fakes;
using Harness::DriverFault::OVERCURRENT;
using Harness::DriverFault::THERMAL_SHUTDOWN;

namespace {
struct CountingCritical : Platform::ICriticalSection {
    int depth = 0, entries = 0;
    void enter() override { ++depth; ++entries; }
    void exit() override { --depth; }
};

struct Rig {
    FakeStepPulseTimer timer;
    FakeGPIO dir, nEn, m0, m1, m2, nFault;
    CountingCritical cs;
    Drivers::StepDirDriver drv;
    explicit Rig(bool faultWired = true) : drv(timer, pins(faultWired), cs)
    {
        nFault.level = true;                     // open drain, pulled up: no fault
        timer.dir = &dir;
    }
    Drivers::StepDirDriver::Pins pins(bool faultWired)
    {
        Drivers::StepDirDriver::Pins p;
        p.dir = &dir;
        p.nEnable = &nEn;
        p.mode[0] = &m0; p.mode[1] = &m1; p.mode[2] = &m2;
        p.nFault = faultWired ? &nFault : nullptr;
        return p;
    }
    unsigned modePins() const { return (m0.level ? 1u : 0u) | (m1.level ? 2u : 0u) | (m2.level ? 4u : 0u); }
    /// 1/16, 2000 full steps/s^2 both ways, 1000 full steps/s ceiling
    Harness::DriverSettings settings(uint16_t microsteps = 16, float accel = 2000.0f, float ceiling = 1000.0f)
    {
        Harness::DriverSettings s = {};
        s.microsteps = microsteps;
        s.mode = Harness::DriveMode::CURRENT;
        s.accelFsps2 = accel;
        s.decelFsps2 = accel;
        s.maxSpeedFsps = ceiling;
        return s;
    }
};

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
}

// ---- Capabilities: what a STEP/DIR chip lacks, said honestly ----

TEST(caps_say_what_a_step_dir_chip_cannot_do) {
    Rig r;
    const Harness::DriverCaps& c = r.drv.caps();
    EXPECT_FALSE(c.voltageControl);
    EXPECT_FALSE(c.currentControl);              // the reference is the VREF pot
    EXPECT_EQ(c.maxMicrostepsVoltage, 0);
    EXPECT_EQ(c.maxMicrostepsCurrent, 32);       // MODE2..0 = 101
    EXPECT_FALSE(c.stallDetect);
    EXPECT_FALSE(c.hardwarePositioning);         // the ramps are the MCU's
    EXPECT_TRUE(c.standbyPin);                   // nENABLE
    EXPECT_EQ(c.syncPulsesPerFullStep, 0);
    EXPECT_EQ(c.faultsReported, OVERCURRENT | THERMAL_SHUTDOWN);
    EXPECT_EQ(c.faultsAlarmable, 0);
    EXPECT_EQ(c.maxCurrentA, 0.0f);
    EXPECT_EQ(c.overcurrentMinA, 3.0f);
    EXPECT_EQ(c.overcurrentMaxA, 3.0f);
    EXPECT_EQ(c.maxSpeedFsps, 1562.5f);          // 1 MHz / 20 ticks / 32 microsteps
    EXPECT_EQ(c.maxSpeedLimitFsps, 1562.5f);
    // The protocol's and L3's range checks refuse what the chip has no input for
    EXPECT_FALSE(Harness::currentWithinCaps(c, 1.0f));
    EXPECT_FALSE(Harness::dutyWithinCaps(c, 0.2f));
    EXPECT_FALSE(Harness::stallWithinCaps(c, 0.5f));
    EXPECT_FALSE(Harness::overcurrentWithinCaps(c, 50.0f));
    EXPECT_TRUE(Harness::overcurrentWithinCaps(c, 3.0f));
    EXPECT_TRUE(Harness::microstepsWithinCaps(c, 32));
    EXPECT_FALSE(Harness::microstepsWithinCaps(c, 64));
    EXPECT_FALSE(c.hasMode(Harness::DriveMode::VOLTAGE));
    EXPECT_EQ(c.maxMicrosteps(Harness::DriveMode::CURRENT), 32);

    Rig unwired(false);
    EXPECT_EQ(unwired.drv.caps().faultsReported, 0);   // no nFAULT line, no faults claimed
}

TEST(quantize_reports_the_values_in_force_for_what_the_chip_cannot_take) {
    Rig r;
    Harness::DriverSettings s = r.settings(128, 2008.2f, 992.3f);
    s.mode = Harness::DriveMode::VOLTAGE;
    s.currentA = Harness::DriveLevels{0.31f, 1.33f, 1.56f, 1.56f};
    s.duty = Harness::DriveLevels{0.1f, 0.2f, 0.3f, 0.3f};
    s.overcurrentA = 100.0f;
    s.stallThreshold01 = 0.5f;
    s.faultAlarms = 0x3F;
    s.tuning.tag = 0x50;                         // another chip's block: kept, not used
    const Harness::DriverSettings q = r.drv.quantize(s);
    EXPECT_EQ(q.mode, Harness::DriveMode::CURRENT);
    EXPECT_EQ(q.microsteps, 32);
    EXPECT_EQ(q.currentA.run, 0.0f);
    EXPECT_EQ(q.duty.run, 0.0f);
    EXPECT_EQ(q.overcurrentA, 3.0f);
    EXPECT_EQ(q.stallThreshold01, 0.0f);
    EXPECT_EQ(q.faultAlarms, OVERCURRENT | THERMAL_SHUTDOWN);
    EXPECT_EQ(q.accelFsps2, 2008.0f);
    EXPECT_EQ(q.maxSpeedFsps, 63507.0f / 64.0f);
    EXPECT_EQ(q.tuning.tag, 0x50);
    const Harness::DriverSettings qq = r.drv.quantize(q);
    EXPECT_TRUE(std::memcmp(&q, &qq, sizeof q) == 0);   // idempotent
    // A positive ceiling or ramp never rounds to 0
    s.maxSpeedFsps = 0.001f;
    s.accelFsps2 = 0.2f;
    EXPECT_EQ(r.drv.quantize(s).maxSpeedFsps, 1.0f / 64.0f);
    EXPECT_EQ(r.drv.quantize(s).accelFsps2, 1.0f);
}

TEST(apply_writes_the_mode_pins_and_reads_them_back) {
    Rig r;
    EXPECT_EQ(r.modePins(), 0u);                 // power-on: full step
    EXPECT_TRUE(r.nEn.level);                    // outputs off
    EXPECT_EQ(r.drv.apply(r.settings(16)), Harness::ApplyResult::APPLIED);
    EXPECT_EQ(r.modePins(), 4u);                 // 1/16 = 100
    Harness::DriverSettings rb = {};
    EXPECT_TRUE(r.drv.readSettings(rb));
    EXPECT_EQ(rb.microsteps, 16);
    EXPECT_EQ(rb.overcurrentA, 3.0f);
    EXPECT_EQ(rb.currentA.hold, 0.0f);
    EXPECT_EQ(r.drv.apply(r.settings(8)), Harness::ApplyResult::APPLIED);
    EXPECT_EQ(r.modePins(), 3u);                 // 1/8 = 011
    EXPECT_EQ(r.drv.apply(r.settings(256)), Harness::ApplyResult::APPLIED);
    EXPECT_EQ(r.modePins(), 5u);                 // capped at 1/32 = 101
    EXPECT_EQ(r.cs.depth, 0);                    // every critical section left
}

// ---- Ramps and the pulse count ----

TEST(run_ramps_up_at_the_acceleration_and_holds_the_speed_exactly) {
    Rig r;
    r.drv.apply(r.settings(16, 2000.0f, 1000.0f));
    r.drv.run(true, 300.0f);                     // 4800 pulses/s: 208.33 ticks
    EXPECT_FALSE(r.nEn.level);                   // energised
    EXPECT_TRUE(r.dir.level);
    EXPECT_TRUE(r.timer.running());
    r.timer.advance(1000000);                    // one second
    const size_t n = r.timer.pulseAt.size();
    EXPECT_TRUE(n > 1000);
    EXPECT_EQ(r.drv.readState().positionUsteps, static_cast<int32_t>(n));
    // The ramp: periods fall, then hold; 300 / 2000 = 0.15 s to the speed
    size_t cruiseAt = 0;
    for (size_t i = 1; i < r.timer.periods.size(); ++i) {
        EXPECT_TRUE(r.timer.periods[i] <= r.timer.periods[i - 1] + 1);
        if (cruiseAt == 0 && r.timer.periods[i] <= 209) cruiseAt = i;
    }
    EXPECT_TRUE(cruiseAt > 0 && cruiseAt < r.timer.pulseAt.size());
    const double tCruise = static_cast<double>(r.timer.pulseAt[cruiseAt]) / 1e6;
    EXPECT_TRUE(near(tCruise, 0.15, 0.015));
    // Cruise: the Q8 fraction dithered, so the average period is exact
    double sum = 0.0;
    const size_t k = 480;
    for (size_t i = r.timer.periods.size() - k; i < r.timer.periods.size(); ++i) sum += r.timer.periods[i];
    EXPECT_TRUE(near(sum / k, 1e6 / 4800.0, 0.01));
    const Drivers::MotorStatus st = r.drv.readState();
    EXPECT_TRUE(st.moving);
    EXPECT_FALSE(st.outputsOff);
    EXPECT_EQ(st.phase, Harness::MotionPhase::CONSTANT);
    EXPECT_TRUE(near(st.speedFsps, 300.0, 0.01));
}

TEST(run_is_capped_by_the_speed_ceiling) {
    Rig r;
    r.drv.apply(r.settings(16, 5000.0f, 200.0f));
    r.drv.run(true, 800.0f);
    r.timer.advance(500000);
    EXPECT_TRUE(near(r.drv.readState().speedFsps, 200.0, 0.01));
    r.drv.setMaxSpeed(400.0f);                   // a raised ceiling: speeds up to the run speed's cap
    r.timer.advance(500000);
    EXPECT_TRUE(near(r.drv.readState().speedFsps, 400.0, 0.01));
}

TEST(move_lands_exactly_and_holds) {
    Rig r;
    r.drv.apply(r.settings(16, 2000.0f, 500.0f));
    r.drv.move(true, 6400);                      // 400 full steps
    const size_t n = r.timer.runToStop();
    EXPECT_EQ(n, 6400u);
    Drivers::MotorStatus st = r.drv.readState();
    EXPECT_EQ(st.positionUsteps, 6400);
    EXPECT_FALSE(st.moving);
    EXPECT_FALSE(st.outputsOff);                 // a finished move holds
    EXPECT_FALSE(r.drv.isBusy());
    uint32_t shortest = 0xFFFFFFFFu;
    for (uint32_t p : r.timer.periods) shortest = p < shortest ? p : shortest;
    EXPECT_TRUE(shortest >= 124);                // 500 x 16 = 8000 pulses/s: 125 ticks, never faster
    EXPECT_TRUE(r.timer.periods.back() > 4 * 125);   // decelerated: the last pulses are slow
    // and back, DIR low on every pulse of it
    const size_t before = r.timer.dirAt.size();
    r.drv.move(false, 6400);
    EXPECT_EQ(r.timer.runToStop(), 6400u);
    EXPECT_EQ(r.drv.readState().positionUsteps, 0);
    bool allBack = true;
    for (size_t i = before; i < r.timer.dirAt.size(); ++i) allBack = allBack && !r.timer.dirAt[i];
    EXPECT_TRUE(allBack);
    // the smallest moves
    r.drv.move(true, 1);
    EXPECT_EQ(r.timer.runToStop(), 1u);
    r.drv.move(true, 2);
    EXPECT_EQ(r.timer.runToStop(), 2u);
    r.drv.goTo(3);
    EXPECT_EQ(r.timer.runToStop(), 0u);          // already there: no pulse, no start
    EXPECT_EQ(r.drv.readState().positionUsteps, 3);
}

TEST(a_reversed_run_decelerates_to_a_standstill_before_dir_changes) {
    Rig r;
    r.drv.apply(r.settings(16, 2000.0f, 1000.0f));
    r.drv.run(true, 500.0f);
    r.timer.advance(500000);
    const int32_t peak0 = r.drv.readState().positionUsteps;
    r.drv.run(false, 500.0f);
    r.timer.advance(1000000);
    EXPECT_FALSE(r.dir.level);
    EXPECT_TRUE(r.drv.readState().positionUsteps < peak0 + 4000);
    size_t flips = 0, flipAt = 0;
    for (size_t i = 1; i < r.timer.dirAt.size(); ++i) {
        if (r.timer.dirAt[i] != r.timer.dirAt[i - 1]) { ++flips; flipAt = i; }
    }
    EXPECT_EQ(flips, 1u);
    // around the reversal the pulses are slow (stopped, then the first step of a ramp)
    const uint64_t gap = r.timer.pulseAt[flipAt] - r.timer.pulseAt[flipAt - 1];
    EXPECT_TRUE(gap > 4 * 125);
    EXPECT_TRUE(near(r.drv.readState().speedFsps, 500.0, 0.01));
    EXPECT_FALSE(r.drv.readState().forward);
}

TEST(a_target_too_close_to_stop_for_is_overshot_and_reached) {
    Rig r;
    r.drv.apply(r.settings(16, 2000.0f, 1000.0f));
    r.drv.run(true, 1000.0f);
    r.timer.advance(1000000);                    // cruising: 250 full steps (4000 usteps) to stop
    const int32_t here = r.drv.readState().positionUsteps;
    r.drv.goTo(here + 100);
    int32_t furthest = here;
    while (r.timer.running()) {
        r.timer.advance(1000);
        const int32_t p = r.drv.readState().positionUsteps;
        furthest = p > furthest ? p : furthest;
    }
    EXPECT_EQ(r.drv.readState().positionUsteps, here + 100);
    EXPECT_TRUE(furthest > here + 3000);         // went past, came back
    EXPECT_FALSE(r.drv.isBusy());
}

TEST(soft_stop_decelerates_and_soft_hiz_releases_at_the_standstill) {
    Rig r;
    r.drv.apply(r.settings(16, 2000.0f, 1000.0f));
    r.drv.run(true, 500.0f);
    r.timer.advance(500000);
    r.drv.softHiZ();
    EXPECT_FALSE(r.nEn.level);                   // still energised while it slows down
    EXPECT_TRUE(r.drv.readState().moving);
    const size_t n = r.timer.runToStop();
    EXPECT_TRUE(n > 1000 && n < 1100);           // 500^2 / (2 x 2000) x 16 = 1000 pulses
    EXPECT_TRUE(r.nEn.level);                    // released at the standstill
    EXPECT_TRUE(r.drv.readState().outputsOff);
    r.drv.run(true, 500.0f);
    r.timer.advance(500000);
    r.drv.softStop();
    r.timer.runToStop();
    EXPECT_FALSE(r.nEn.level);                   // a soft stop holds
    EXPECT_FALSE(r.drv.readState().moving);
}

TEST(estop_standby_stops_the_pulses_at_once_and_drops_the_bridges) {
    Rig r;
    r.drv.apply(r.settings(16));
    r.drv.run(true, 500.0f);
    r.timer.advance(300000);
    const size_t n = r.timer.pulseAt.size();
    r.drv.setStandby(true);                      // the out-of-band E-STOP, from an ISR
    EXPECT_FALSE(r.timer.running());
    EXPECT_TRUE(r.nEn.level);
    r.timer.advance(300000);
    EXPECT_EQ(r.timer.pulseAt.size(), n);        // not one more pulse
    EXPECT_EQ(r.drv.onPulseIsr(), 0u);           // a pulse interrupt already in flight ends it too
    Drivers::MotorStatus st = r.drv.readState();
    EXPECT_FALSE(st.moving);
    EXPECT_TRUE(st.outputsOff);
    EXPECT_EQ(st.speedFsps, 0.0f);
    EXPECT_EQ(st.positionUsteps, static_cast<int32_t>(n));   // the count survives
    // L3's wake: release standby, init, apply; motion works again
    r.drv.setStandby(false);
    r.drv.init();
    r.drv.apply(r.settings(16));
    r.drv.run(true, 500.0f);
    r.timer.advance(100000);
    EXPECT_TRUE(r.timer.pulseAt.size() > n);
    // hard Hi-Z: the same, from task context
    r.drv.hardHiZ();
    EXPECT_FALSE(r.timer.running());
    EXPECT_TRUE(r.nEn.level);
    EXPECT_FALSE(r.drv.isBusy());
    // hard stop: pulses stop, bridges hold
    r.drv.run(true, 500.0f);
    r.timer.advance(100000);
    r.drv.hardStop();
    EXPECT_FALSE(r.timer.running());
    EXPECT_FALSE(r.nEn.level);
}

TEST(a_new_resolution_waits_for_a_standstill_and_keeps_the_shaft_angle) {
    Rig r;
    r.drv.apply(r.settings(16));
    r.drv.move(true, 1600);
    r.timer.runToStop();
    r.drv.run(true, 300.0f);
    r.timer.advance(200000);
    EXPECT_EQ(r.drv.apply(r.settings(8)), Harness::ApplyResult::NEEDS_OUTPUTS_OFF);
    EXPECT_EQ(r.modePins(), 4u);                 // unchanged while pulses run
    Harness::DriverSettings rb = {};
    r.drv.readSettings(rb);
    EXPECT_EQ(rb.microsteps, 16);
    r.drv.hardHiZ();                             // L3: release, apply, energise
    const int32_t at16 = r.drv.readState().positionUsteps;
    EXPECT_EQ(r.drv.apply(r.settings(8)), Harness::ApplyResult::APPLIED);
    r.drv.softStop();
    EXPECT_EQ(r.modePins(), 3u);
    EXPECT_EQ(r.drv.readState().positionUsteps, at16 / 2);
    // at a standstill with the bridges on, the pins change directly (the indexer keeps its angle)
    EXPECT_FALSE(r.nEn.level);
    EXPECT_EQ(r.drv.apply(r.settings(32)), Harness::ApplyResult::APPLIED);
    EXPECT_EQ(r.modePins(), 5u);
}

TEST(faults_come_from_the_nfault_line) {
    Rig r;
    EXPECT_EQ(r.drv.readState().faults, 0);
    EXPECT_FALSE(r.drv.flagActive());
    r.nFault.level = false;                      // overcurrent or thermal shutdown: the chip won't say
    const Drivers::MotorStatus st = r.drv.readState();
    EXPECT_EQ(st.faults, OVERCURRENT | THERMAL_SHUTDOWN);
    EXPECT_TRUE(st.shutdownFault());
    EXPECT_TRUE(r.drv.flagActive());
}

TEST(no_chip_units_no_reset_watch_and_its_own_diagnostics) {
    Rig r;
    Drivers::LegacyDriverUnits u = {};
    Harness::DriverSettings s = r.settings();
    EXPECT_FALSE(r.drv.toLegacyUnits(s, u));     // protocol 2's codes are the powerSTEP01's
    EXPECT_FALSE(r.drv.fromLegacyUnits(u, s));
    EXPECT_FALSE(r.drv.checkReset());
    char buf[Harness::MotorDebugParams::TEXT_MAX];
    EXPECT_TRUE(r.drv.diagnostics(Harness::DriverDiagView::REGISTERS, buf, sizeof buf) > 0);
    EXPECT_TRUE(std::string(buf).find("STEP/DIR") != std::string::npos);
    EXPECT_TRUE(r.drv.diagnostics(Harness::DriverDiagView::SUMMARY, buf, sizeof buf) > 0);
}

TEST(the_speed_ceiling_round_trips_exactly) {
    Rig r;
    r.drv.setMaxSpeed(992.3f);
    const float got = r.drv.getMaxSpeed();
    EXPECT_EQ(got, 63507.0f / 64.0f);
    r.drv.setMaxSpeed(got);
    EXPECT_EQ(r.drv.getMaxSpeed(), got);         // L3's per-move override relies on it
    r.drv.setMaxSpeed(0.001f);
    EXPECT_EQ(r.drv.getMaxSpeed(), 1.0f / 64.0f);
    r.drv.setMaxSpeed(1e9f);
    EXPECT_EQ(r.drv.getMaxSpeed(), 1562.5f);
}

TEST(reset_pos_keeps_a_moving_target_distance) {
    Rig r;
    r.drv.apply(r.settings(16));
    r.drv.move(true, 3200);
    r.timer.advance(200000);
    const int32_t at = r.drv.readState().positionUsteps;
    r.drv.resetPos();
    r.timer.runToStop();
    EXPECT_EQ(r.drv.readState().positionUsteps, 3200 - at);
}
