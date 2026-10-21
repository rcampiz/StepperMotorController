/**
 * @file test_encoder_service.cpp
 * @brief Step 4b: the Encoder task body now lives in L3 EncoderService and runs on the host
 *        against a fake Drivers::IEncoderHardware (upper side of H_L3_L4) and Platform fakes.
 */
#include "minitest.hpp"
#include "L3_services/motion/encoder_service/encoder_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <vector>

namespace {

struct FakeEncoderHw : Drivers::IEncoderHardware {
    std::vector<uint16_t> ring = std::vector<uint16_t>(256, 0);
    uint32_t writeIdx = 0; uint16_t rate = 1000; bool ready = true;
    int resets = 0; Drivers::IndexSnapshot idx{}; int32_t count = 1234;
    FakeEncoderHw() { idx.tickMs = 77; idx.revolutions = 3; idx.periodUs = 5000; }
    bool isReady() const override { return ready; }
    int32_t getCount() const override { return count; }   // the live counter (driver: int16 sign-extended)
    void reset() override { ++resets; }
    Drivers::IndexSnapshot readIndex() const override { return idx; }
    void indexISR(uint32_t, uint32_t) override {}
    const uint16_t* sampleBuffer() const override { return ring.data(); }
    uint32_t sampleBufferSize() const override { return static_cast<uint32_t>(ring.size()); }
    uint32_t sampleWriteIndex() const override { return writeIdx; }
    uint16_t sampleRateHz() const override { return rate; }
    void restartSampling(uint16_t hz) override { rate = hz; }
};

struct FakeCritical : Platform::ICriticalSection {
    int depth = 0, maxDepth = 0;
    void enter() override { ++depth; if (depth > maxDepth) maxDepth = depth; }
    void exit() override { --depth; }
};

struct FakeUsClock : Platform::IMicrosecondClock {
    uint32_t now = 1000;
    uint32_t getTickUs() const override { return now; }
};

struct FakeProcessing : Services::IEncoderProcessing {
    int fed = 0; int64_t position = 0; int32_t lastRaw = 0;
    void feedSample(uint16_t raw, uint32_t) override { ++fed; position = raw; lastRawCnt = raw; }
    int32_t filter(int32_t raw, uint32_t) override { lastRaw = raw; return raw * 10; }   // "filter" = x10
    void reset() override { position = 0; }
    void setFilterConfig(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) override {}
    void getFilterConfig(Harness::EncoderFilterConfig& cfg) const override { cfg = Harness::EncoderFilterConfig{}; }
    void setFilter(uint8_t, uint8_t) override {}
    void getFilter(uint8_t& t, uint8_t& p) const override { t = 0; p = 0; }
    int64_t getPosition() const override { return position; }
    uint16_t lastRawCount() const override { return lastRawCnt; }
    uint16_t lastRawCnt = 0;
    VelocityQuality getQuality() const override { return static_cast<VelocityQuality>(0); }
    uint8_t getFilterFlags() const override { return 0; }
};

struct FakeStatusSink : Services::IEncoderStatusSink {
    Harness::EncoderStatus last = Harness::EncoderStatus::NOT_PRESENT;
    void setEncoderStatus(Harness::EncoderStatus s) override { last = s; }
};

struct Rig {
    FakeEncoderHw hw; FakeCritical crit; FakeUsClock us; FakeProcessing proc; FakeStatusSink sink;
    Services::EncoderService svc;
    Platform::NullLock lock; Platform::NullClock clk;
    Rig() { Services::g_telemetryStore.init(lock, clk); }
    Protocol::TelemetrySnapshot snapshot() { return Services::g_telemetryStore.getSnapshot(); }
    bool init(uint8_t windowMs = 40, uint16_t rateHz = 1000) {
        return svc.init(hw, crit, us, proc, sink, windowMs, rateHz);
    }
};

} // namespace

TEST(init_reports_ready_through_the_status_sink) {
    Rig r;
    EXPECT_TRUE(r.init());
    EXPECT_EQ(r.sink.last, Harness::EncoderStatus::READY);
    EXPECT_TRUE(r.svc.isAvailable());
}

TEST(count_now_adds_the_counter_movement_since_the_last_sample) {
    // The motor task pairs this with each position read: the sampled count is up to 10 ms old
    Rig r; r.init();
    r.hw.ring[0] = 100; r.hw.writeIdx = 1;
    r.svc.sample();                                // count 100 at raw 100 (fake: position = raw)
    r.hw.count = 130;                              // the shaft has moved on 30 counts since
    EXPECT_EQ(r.svc.countNow(), 130);
    r.hw.count = 90;                               // or back 10
    EXPECT_EQ(r.svc.countNow(), 90);
    r.hw.ring[1] = 65530; r.hw.writeIdx = 2;
    r.svc.sample();                                // count 65530 at raw 65530
    r.hw.count = 4;                                // across the 16-bit wrap: +10
    EXPECT_EQ(r.svc.countNow(), 65540);
    r.hw.count = -6;                               // the driver's sign-extended form of 65530
    EXPECT_EQ(r.svc.countNow(), 65530);
    EXPECT_EQ(r.crit.depth, 0);
}

TEST(init_without_hardware_reports_not_present) {
    Rig r; r.hw.ready = false;
    EXPECT_FALSE(r.init());
    EXPECT_EQ(r.sink.last, Harness::EncoderStatus::NOT_PRESENT);
}

TEST(sample_feeds_only_new_ring_entries_and_computes_window_velocity) {
    Rig r; r.init(40, 1000);                       // window = 40 samples
    for (uint32_t i = 0; i < 100; ++i) r.hw.ring[i] = static_cast<uint16_t>(i * 2);   // +2 counts/sample
    r.hw.writeIdx = 100;

    r.svc.sample();
    EXPECT_EQ(r.proc.fed, 100);                    // entries 0..99
    EXPECT_EQ(r.svc.lastRawVelocity(), 2000);      // (ring[99]-ring[59]) x 1000 Hz / 40 samples: counts/s
    EXPECT_EQ(r.svc.getState().velocity, 20000);   // filtered = raw x10
    EXPECT_EQ(r.snapshot().encoder.sampleRateHz, 1000u);
    EXPECT_EQ(r.snapshot().encoder.revolutions, 3);
    EXPECT_EQ(r.snapshot().encoder.velocity, 20000);

    r.svc.sample();                                // nothing new
    EXPECT_EQ(r.proc.fed, 100);
}

TEST(snapshot_reads_are_critical_section_balanced) {
    Rig r; r.init();
    r.hw.writeIdx = 5;
    r.svc.sample();
    (void)r.svc.getState();
    r.svc.resetCount();
    r.svc.clearIndexFlag();
    EXPECT_EQ(r.crit.depth, 0);
    EXPECT_EQ(r.crit.maxDepth, 1);
    EXPECT_EQ(r.hw.resets, 1);
}

TEST(changing_the_sample_rate_restarts_hardware_sampling) {
    Rig r; r.init(40, 1000);
    Harness::EncoderFilterConfig cfg{};
    cfg.measWindowMs = 20; cfg.sampleRateHz = 2000;
    r.svc.setConfig(cfg);
    EXPECT_EQ(r.hw.rate, 2000u);
    Harness::EncoderFilterConfig back{};
    r.svc.getConfig(back);
    EXPECT_EQ(back.measWindowMs, 20u);
    EXPECT_EQ(back.sampleRateHz, 2000u);
}
