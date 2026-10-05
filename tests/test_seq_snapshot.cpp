/**
 * @file test_seq_snapshot.cpp
 * @brief Index snapshot is never torn: a write landing mid-read forces a clean retry, and the
 *        L4 Encoder publishes pulses/tick/revolutions/period as one consistent unit.
 */
#include "minitest.hpp"
#include "harness/pins/seq_snapshot.hpp"
#include "L4_drivers/devices/encoder.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include <vector>

namespace {

using Drivers::IndexSnapshot;

IndexSnapshot snap(uint32_t pulses, uint32_t tick, int32_t rev, uint32_t period) {
    IndexSnapshot s; s.pulses = pulses; s.tickMs = tick; s.revolutions = rev; s.periodUs = period; return s;
}

struct FakeTimer : Board::ITimerCounter {
    uint16_t count = 0; bool down = false;
    uint16_t getCount() const override { return count; }
    void setCount(uint16_t v) override { count = v; }
    bool isCountingDown() const override { return down; }
};

struct FakeSampler : Board::IEncoderSampler {
    std::vector<uint16_t> ring = std::vector<uint16_t>(256, 0);
    uint16_t rate = 0; bool running = false;
    void start(uint16_t hz) override { rate = hz; running = true; }
    void stop() override { running = false; }
    void setSampleRate(uint16_t hz) override { rate = hz; }
    uint16_t getSampleRateHz() const override { return rate; }
    uint32_t getWriteIndex() const override { return 0; }
    const uint16_t* getBuffer() const override { return ring.data(); }
    uint32_t getBufferSize() const override { return static_cast<uint32_t>(ring.size()); }
    void prefillBuffer(uint16_t v) override { for (auto& x : ring) x = v; }
};

} // namespace

TEST(a_write_landing_mid_read_forces_a_clean_retry) {
    Harness::SeqSnapshot<IndexSnapshot> s;
    s.write(snap(1, 100, 1, 0));

    int hooks = 0;
    IndexSnapshot out = s.read([&] {
        if (hooks++ == 0) s.write(snap(2, 200, 2, 100));   // "ISR" fires between copy and check
    });

    EXPECT_EQ(hooks, 2);               // first attempt invalidated, second attempt clean
    EXPECT_EQ(out.pulses, 2u);         // all four fields from the same write
    EXPECT_EQ(out.tickMs, 200u);
    EXPECT_EQ(out.revolutions, 2);
    EXPECT_EQ(out.periodUs, 100u);
    EXPECT_EQ(s.sequence() % 2u, 0u);
}

TEST(a_quiet_read_takes_one_attempt) {
    Harness::SeqSnapshot<IndexSnapshot> s;
    s.write(snap(7, 70, 7, 700));
    int hooks = 0;
    IndexSnapshot out = s.read([&] { ++hooks; });
    EXPECT_EQ(hooks, 1);
    EXPECT_EQ(out.pulses, 7u);
    EXPECT_EQ(out.periodUs, 700u);
}

TEST(encoder_index_isr_publishes_one_consistent_snapshot) {
    FakeTimer timer; FakeSampler sampler;
    Platform::NullClock clock; Platform::NullMicrosecondClock us;
    Harness::EncoderPins pins; pins.timer = &timer; pins.sampler = &sampler; pins.indexExtiLine = 9;
    Drivers::Encoder enc(pins, clock, us, nullptr);
    EXPECT_TRUE(enc.init(1000));
    EXPECT_TRUE(sampler.running);

    IndexSnapshot before = enc.readIndex();
    EXPECT_EQ(before.pulses, 0u);

    enc.indexISR(10, 1000);
    enc.indexISR(20, 6000);
    IndexSnapshot idx = enc.readIndex();
    EXPECT_EQ(idx.pulses, 2u);
    EXPECT_EQ(idx.tickMs, 20u);
    EXPECT_EQ(idx.revolutions, 2);
    EXPECT_EQ(idx.periodUs, 5000u);

    timer.down = true;
    enc.indexISR(30, 7000);
    EXPECT_EQ(enc.readIndex().revolutions, 1);
    EXPECT_EQ(enc.readIndex().periodUs, 1000u);
}
