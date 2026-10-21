/**
 * @file test_encoder_observer.cpp
 * @brief The tracking observer in EncoderProcessor (ENC_FILT_OBSERVER): follows a constant
 *        speed, a speed step within a few ms, re-seats on a jump, stays at 0 at rest.
 */
#include "minitest.hpp"
#include "L3_services/motion/encoder_processor/encoder_processor.hpp"
#include <math.h>

namespace {

constexpr uint32_t RATE_HZ = 2000;
constexpr uint32_t PERIOD_US = 1000000 / RATE_HZ;

/// Feed samples of a shaft at `cps` counts/s for `ms`, then one filter() call per 10 ms task pass
struct Shaft {
    Services::EncoderProcessor p;
    double pos = 0.0;          // true position, counts
    uint32_t t = 1000;         // us
    int32_t last = 0;
    explicit Shaft(uint8_t bw) {
        p.setFilterConfig(Harness::ENC_FILT_OBSERVER, 0, 0, 0, 0, 0, 0, 0, 0, 0, bw);
    }
    void run(double cps, uint32_t ms) {
        for (uint32_t i = 0; i < ms * RATE_HZ / 1000; ++i) {
            pos += cps / RATE_HZ;
            t += PERIOD_US;
            p.feedSample(static_cast<uint16_t>(static_cast<int64_t>(floor(pos)) & 0xFFFF), t);
            if (i % (RATE_HZ / 100) == 0) last = p.filter(0, t);
        }
    }
};

} // namespace

TEST(observer_follows_a_constant_speed) {
    Shaft s(20);
    s.run(10000.0, 1000);                          // 150 RPM at 4000 counts/rev
    EXPECT_TRUE(fabs(s.last - 10000.0) < 100.0);   // within 1 %
}

TEST(observer_settles_on_a_speed_step_within_tens_of_ms) {
    Shaft s(20);
    s.run(2667.0, 500);                            // 40 RPM
    s.run(10000.0, 40);                            // step to 150 RPM: 40 ms later
    EXPECT_TRUE(s.last > 2667 + 0.6 * (10000 - 2667));
    s.run(10000.0, 100);
    EXPECT_TRUE(fabs(s.last - 10000.0) < 200.0);
}

TEST(observer_reads_zero_at_rest_and_reseats_on_a_jump) {
    Shaft s(20);
    s.run(0.0, 300);
    EXPECT_EQ(s.last, 0);
    s.pos += 30000.0;                              // a counter jump no shaft makes
    s.run(0.0, 300);
    EXPECT_EQ(s.last, 0);                          // re-seated, no speed spike left behind
}

TEST(observer_keeps_its_bandwidth_and_reports_it) {
    Services::EncoderProcessor p;
    p.setFilterConfig(Harness::ENC_FILT_OBSERVER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 45);
    Harness::EncoderFilterConfig c{};
    p.getFilterConfig(c);
    EXPECT_EQ(c.observerBwHz, 45);
    p.setFilterConfig(Harness::ENC_FILT_OBSERVER, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    p.getFilterConfig(c);
    EXPECT_EQ(c.observerBwHz, Harness::ENC_FILT_DEFAULT_OBSERVER_HZ);   // 0 = the default
}

TEST(ripple_reads_a_wobble_and_ignores_a_steady_run) {
    Shaft s(10);
    s.run(8000.0, 500);                            // a straight run: only the counter's steps
    const float flat = s.p.getRippleCounts();
    EXPECT_TRUE(flat < 0.6f);
    // the same run with a 130 Hz wobble of 5 counts on the shaft
    Services::EncoderProcessor p;
    double pos = 0.0; uint32_t t = 1000;
    for (int i = 0; i < 1000; ++i) {
        pos += 8000.0 / RATE_HZ; t += PERIOD_US;
        const double w = 5.0 * sin(2.0 * 3.14159265 * 130.0 * i / RATE_HZ);
        p.feedSample(static_cast<uint16_t>(static_cast<int64_t>(floor(pos + w)) & 0xFFFF), t);
    }
    EXPECT_TRUE(p.getRippleCounts() > 2.5f && p.getRippleCounts() < 4.5f);   // rms of a 5-count sine: 3.5
}
