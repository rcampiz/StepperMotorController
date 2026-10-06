/**
 * @file test_comms_service.cpp
 * @brief Step 7: the Comms task body runs in L3 CommsService against a fake L2 processor
 *        (H_L2_L3 upward signal) and Platform fakes.
 */
#include "minitest.hpp"
#include "L3_services/comms/comms_service/comms_service.hpp"
#include <vector>
#include <string>

namespace {

struct FakeProcessor : Protocol::ICommandProcessor {
    int processed = 0, banners = 0, telemetryLines = 0;
    std::vector<uint32_t> eventSeqs;
    void process() override { ++processed; }
    void checkBaudRevert() override {}
    void emitBanner() override { ++banners; }
    void emitEvent(const AsyncEvent&, uint32_t seq, uint32_t) override { eventSeqs.push_back(seq); }
    void emitTelemetry(const Protocol::TelemetrySnapshot&) override { ++telemetryLines; }
    void emitJoystick(UI::JoyDirection, bool) override {}
    void getDispatchStats(Harness::DispatchStats& out) const override { out = Harness::DispatchStats{}; out.totalCommands = 42; }
};

struct FakeClock : Platform::NullClock {
    uint32_t now = 1000;
    uint32_t getTickMs() const override { return now; }
};

struct FakeEvents : Services::IMotorEventReceiver {
    std::vector<AsyncEvent> pending;
    bool receive(AsyncEvent& e) override {
        if (pending.empty()) return false;
        e = pending.front(); pending.erase(pending.begin()); return true;
    }
};

struct FakeEstop : Services::IEmergencyStop {
    int stops = 0;
    void emergencyStop() override { ++stops; }
};

struct Rig {
    FakeProcessor proc; FakeClock clock; FakeEvents events; FakeEstop estop;
    Platform::NullLock lock; Services::TelemetryStore store;
    Services::CommsService svc;
    Rig() {
        store.init(lock, clock);
        svc.init(proc, clock, events, estop, store, nullptr, nullptr);
    }
};

} // namespace

TEST(start_emits_banner_and_tick_drives_the_parser) {
    Rig r;
    r.svc.onStart();
    r.svc.tick(); r.svc.tick();
    EXPECT_EQ(r.proc.banners, 1);
    EXPECT_EQ(r.proc.processed, 2);
}

TEST(events_are_drained_with_monotonic_sequence_numbers) {
    Rig r;
    r.events.pending = { AsyncEvent{EventType::FAULT, 1}, AsyncEvent{EventType::STALL, 2} };
    r.svc.tick();
    EXPECT_EQ(r.proc.eventSeqs.size(), 2u);
    EXPECT_EQ(r.proc.eventSeqs[0], 1u);
    EXPECT_EQ(r.proc.eventSeqs[1], 2u);
    EXPECT_EQ(r.svc.getLastEventSeq(), 2u);
}

TEST(heartbeat_timeout_fires_estop_exactly_once) {
    Rig r;
    EXPECT_EQ(r.svc.setHeartbeatTimeout(500), 500u);
    r.clock.now += 400; r.svc.tick();
    EXPECT_EQ(r.estop.stops, 0);
    r.clock.now += 200; r.svc.tick(); r.svc.tick();
    EXPECT_EQ(r.estop.stops, 1);
    EXPECT_TRUE(r.svc.commsTimedOut());
    r.svc.clearCommsTimeout();
    EXPECT_FALSE(r.svc.commsTimedOut());
}

TEST(heartbeat_received_resets_the_watchdog) {
    Rig r;
    r.svc.setHeartbeatTimeout(500);
    r.clock.now += 450; r.svc.heartbeatReceived(7);
    r.clock.now += 450; r.svc.tick();
    EXPECT_EQ(r.estop.stops, 0);
    bool en; uint32_t to, seq, rem; bool out;
    r.svc.getHeartbeatStatus(en, to, seq, rem, out);
    EXPECT_TRUE(en); EXPECT_EQ(seq, 7u); EXPECT_EQ(rem, 50u); EXPECT_FALSE(out);
}

TEST(telemetry_period_is_clamped_and_zero_turns_it_off) {          // F9
    Rig r;
    EXPECT_FALSE(r.svc.isTelemetryEnabled());                          // off at boot
    EXPECT_EQ(r.svc.setTelemetryPeriod(5), Services::CommsService::TELEMETRY_PERIOD_MIN_MS);
    EXPECT_EQ(r.svc.setTelemetryPeriod(99999), Services::CommsService::TELEMETRY_PERIOD_MAX_MS);
    EXPECT_EQ(r.svc.setTelemetryPeriod(250), 250u);
    EXPECT_EQ(r.svc.getTelemetryPeriod(), 250u);
    EXPECT_TRUE(r.svc.isTelemetryEnabled());
    r.clock.now += 240; r.svc.tick();
    EXPECT_EQ(r.proc.telemetryLines, 0);
    r.clock.now += 20;  r.svc.tick();
    EXPECT_EQ(r.proc.telemetryLines, 1);
    EXPECT_EQ(r.svc.setTelemetryPeriod(0), 0u);
    EXPECT_FALSE(r.svc.isTelemetryEnabled());
    r.clock.now += 1000; r.svc.tick();
    EXPECT_EQ(r.proc.telemetryLines, 1);
}

TEST(telemetry_publishes_every_100ms_when_enabled) {
    Rig r;
    r.svc.tick();
    EXPECT_EQ(r.proc.telemetryLines, 0);
    r.svc.enableTelemetry(true);
    r.clock.now += 50;  r.svc.tick();
    r.clock.now += 60;  r.svc.tick();
    EXPECT_EQ(r.proc.telemetryLines, 1);
}

TEST(dispatch_stats_come_from_the_parser) {
    Rig r;
    Harness::DispatchStats s{};
    r.svc.getStats(s);
    EXPECT_EQ(s.totalCommands, 42u);
}
