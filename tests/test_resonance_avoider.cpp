/**
 * @file test_resonance_avoider.cpp
 * @brief ResonanceAvoider on the bench model (2026-10-21: f_n = 137.1 Hz x I^0.512, run 1.014 A,
 *        78 mA steps): the k = 2 resonance at 276 full steps/s (83 RPM) is moved off the speed
 *        by the smallest raise, else the smallest guarded cut, else left alone.
 */
#include "minitest.hpp"
#include "L3_services/motion/resonance_avoider/resonance_avoider.hpp"
#include <math.h>

namespace {

using Harness::ResonanceState;

constexpr float RUN_A = 1.014f;
constexpr float STEP_A = 0.078f;

Harness::ResonanceConfig model(uint8_t boostPct, uint8_t cutPct = 30) {
    Harness::ResonanceConfig c{};
    c.enabled = true;
    c.harmonicMask = Harness::RES_DEFAULT_HARMONICS;
    c.f1AHz = 137.1f;
    c.exponent = 0.512f;
    c.bandPct = 8;
    c.boostPct = boostPct;
    c.cutPct = cutPct;
    return c;
}

Services::ResonanceInput at(float fsps, bool encoder = true, int32_t fe = 0) {
    Services::ResonanceInput in{};
    in.speedFsps = fsps;
    in.runA = RUN_A;
    in.stepA = STEP_A;
    in.maxA = 9.9f;
    in.guardAvailable = encoder;
    in.followError = fe;
    in.countsPerFullStep = 20.0f;
    return in;
}

struct Rig {
    Services::ResonanceAvoider a;
    explicit Rig(uint8_t boostPct, uint8_t cutPct = 30) {
        a.configure(model(boostPct, cutPct));
        a.resync(RUN_A);
    }
};

bool near(float a, float b) { return fabsf(a - b) < 0.002f; }

} // namespace

TEST(the_model_puts_the_k2_resonance_at_83_rpm) {
    Rig r(25);
    EXPECT_TRUE(fabsf(r.a.resonantFsps(RUN_A, 2) - 276.2f) < 1.0f);    // 82.9 RPM
    EXPECT_TRUE(fabsf(r.a.resonantFsps(RUN_A, 4) - 138.1f) < 1.0f);
}

TEST(clear_of_every_band_the_run_current_stays) {
    Rig r(25);
    Services::ResonanceDecision d = r.a.update(at(200.0f));            // 60 RPM
    EXPECT_EQ(r.a.state(), ResonanceState::NOMINAL);
    EXPECT_TRUE(near(d.runA, RUN_A));
    EXPECT_FALSE(d.write);
}

TEST(in_a_band_the_smallest_raise_that_clears_it_wins) {
    Rig r(40);                                                          // up to 1.42 A
    Services::ResonanceDecision d = r.a.update(at(276.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::BOOST);
    EXPECT_TRUE(near(d.runA, RUN_A + 4 * STEP_A));                      // 1.326 A: +31 %
    EXPECT_TRUE(d.write);
    EXPECT_FALSE(r.a.update(at(276.0f)).write);                         // no rewrite while it holds
}

TEST(a_raise_beyond_the_limit_falls_back_to_the_smallest_cut_once_the_speed_is_steady) {
    Rig r(25);                                                          // +31 % not allowed
    Services::ResonanceDecision d = r.a.update(at(276.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);                // just arrived: no cut yet
    EXPECT_TRUE(near(d.runA, RUN_A));
    for (int i = 0; i < 70; ++i) {
        const Services::ResonanceDecision e = r.a.update(at(276.0f));
        if (e.write) d = e;
    }
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
    EXPECT_TRUE(near(d.runA, RUN_A - 3 * STEP_A));                      // 0.78 A: -23 % (bench: ripple 7.1 -> 2.7)
}

TEST(a_joystick_sweeping_through_the_band_never_gets_a_cut) {
    // The speed keeps changing (finger on the stick): raise or keep, never cut
    Rig r(25);
    float v = 150.0f;
    for (int i = 0; i < 400; ++i) {
        v += (i / 40) % 2 ? -1.5f : 1.5f;                               // 0.5-1 % per call, back and forth
        r.a.update(at(v, true, -29));
        EXPECT_TRUE(r.a.state() != ResonanceState::CUT);
    }
    EXPECT_EQ(r.a.guardTrips(), 0u);
}

TEST(without_an_encoder_there_is_no_cut) {
    Rig r(25);
    Services::ResonanceDecision d = r.a.update(at(276.0f, false));
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);
    EXPECT_TRUE(near(d.runA, RUN_A));
}

TEST(a_growing_lag_ends_the_cut_and_locks_it_out_in_this_band) {
    Rig r(25);
    for (int i = 0; i < 200; ++i) r.a.update(at(276.0f, true, 1000));  // steady, cut, settled baseline
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
    Services::ResonanceDecision d{};
    for (int i = 0; i < 200; ++i) d = r.a.update(at(276.0f, true, 1010)); // +10 counts = 0.5 step
    EXPECT_EQ(r.a.guardTrips(), 1u);
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);
    EXPECT_TRUE(near(d.runA, RUN_A));                                   // torque first
    for (int i = 0; i < 20; ++i) d = r.a.update(at(278.0f, true, 1010));
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);                // no second cut here
    r.a.update(at(200.0f));                                             // out of the band...
    for (int i = 0; i < 70; ++i) d = r.a.update(at(276.0f, true, 1010)); // ...and back, steady: allowed again
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
}

TEST(a_ramp_into_the_band_does_not_trip_the_guard) {
    // Bench 2026-10-21: stepping 80 -> 85 RPM moved the lag by itself while accelerating;
    // judged mid-ramp it read as a growing lag (5 false trips)
    Rig r(25);
    for (int i = 0; i < 40; ++i) r.a.update(at(200.0f, true, -29));   // steady at 60 RPM
    float v = 200.0f;
    for (int i = 0; i < 20; ++i) {                                      // accelerate into the band
        v += 4.0f;
        r.a.update(at(v, true, -29 - i));                               // the lag moves while ramping
    }
    for (int i = 0; i < 200; ++i) r.a.update(at(280.0f, true, -31));   // steady again, settled lag
    EXPECT_EQ(r.a.guardTrips(), 0u);
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
}

TEST(a_cut_that_takes_too_much_margin_at_steady_speed_is_dropped) {
    // Steady just outside the band (299 fsps; the 8 % band reaches 298.3), then a 0.27 % step in:
    // still steady, so the lag before the cut at this speed is the reference; after the cut
    // the lag settles 0.6 step larger
    Rig r(25);
    for (int i = 0; i < 100; ++i) r.a.update(at(299.0f, true, -29));
    EXPECT_EQ(r.a.state(), ResonanceState::NOMINAL);
    r.a.update(at(298.2f, true, -29));
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
    for (int i = 0; i < 200; ++i) r.a.update(at(298.2f, true, -41));  // 0.6 step more
    EXPECT_EQ(r.a.guardTrips(), 1u);
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);
}

TEST(the_band_is_left_with_hysteresis) {
    Rig r(40);
    r.a.update(at(276.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::BOOST);
    // 300: still inside the run current's band + margin, but now near the raised resonance
    // (317): searched again, no raise clears it; once steady the smallest cut does (-8 %, f_n 265)
    Services::ResonanceDecision d = r.a.update(at(300.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::UNAVOIDABLE);
    for (int i = 0; i < 70; ++i) {
        const Services::ResonanceDecision e = r.a.update(at(300.0f));
        if (e.write) d = e;
    }
    EXPECT_EQ(r.a.state(), ResonanceState::CUT);
    EXPECT_TRUE(near(d.runA, RUN_A - STEP_A));
    d = r.a.update(at(320.0f));                                         // 15.9 %: clear
    EXPECT_EQ(r.a.state(), ResonanceState::NOMINAL);
    EXPECT_TRUE(near(d.runA, RUN_A));
    EXPECT_TRUE(d.write);
}

TEST(standstill_disabled_and_unsupported_keep_the_run_current) {
    Rig r(40);
    EXPECT_TRUE(near(r.a.update(at(5.0f)).runA, RUN_A));                // crawling
    Harness::ResonanceConfig off = model(40);
    off.enabled = false;
    r.a.configure(off);
    EXPECT_EQ(r.a.state(), ResonanceState::OFF);
    EXPECT_TRUE(near(r.a.update(at(276.0f)).runA, RUN_A));
    r.a.configure(model(40));
    r.a.markUnsupported();                                              // voltage mode
    EXPECT_TRUE(near(r.a.update(at(276.0f)).runA, RUN_A));
    EXPECT_EQ(r.a.state(), ResonanceState::OFF);
}

TEST(an_apply_resyncs_to_the_new_run_current) {
    Rig r(40);
    r.a.update(at(276.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::BOOST);
    r.a.resync(RUN_A);                                                  // DRV:CFG:APPLY wrote 1.014 A
    Services::ResonanceDecision d = r.a.update(at(276.0f));
    EXPECT_EQ(r.a.state(), ResonanceState::BOOST);
    EXPECT_TRUE(d.write);                                               // the raise is written again
}
