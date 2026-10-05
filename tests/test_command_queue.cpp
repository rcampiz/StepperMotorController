/**
 * @file test_command_queue.cpp
 * @brief Step 7: ARM/START flush is bounded and ESTOP bypasses the motor queue.
 */
#include "minitest.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include <vector>

namespace {

struct FakeSink : Services::IMotorCommandSink {
    std::vector<Services::MotorCommand> sent;
    size_t capacity = 8;
    std::vector<uint32_t> timeouts;
    bool sendCommand(const Services::MotorCommand& cmd, uint32_t timeoutMs) override {
        timeouts.push_back(timeoutMs);
        if (sent.size() >= capacity) return false;   // "queue full"
        sent.push_back(cmd); return true;
    }
};

struct FakeImmediate : Services::IImmediateStop {
    int hiZ = 0;
    void immediateHardHiZ() override { ++hiZ; }
};

struct Rig {
    Platform::NullLock lock; Platform::NullClock clock; Platform::NullMicrosecondClock us;
    FakeSink sink; FakeImmediate immediate;
    Services::CommandQueue q;
    Rig() { Services::TickTimer_Init(us); q.init(lock, clock, sink, &immediate); }
    Services::MotorCommand move(int32_t steps) { return Services::MotorCommand{Services::MotorCmdType::Move, steps, 0}; }
};

} // namespace

TEST(start_flushes_everything_when_the_motor_queue_has_room) {
    Rig r;
    for (int i = 0; i < 3; ++i) EXPECT_EQ(r.q.queueCommand(r.move(100 + i)), Services::QueueResult::OK);
    EXPECT_EQ(r.q.arm(), Services::QueueResult::OK);
    EXPECT_EQ(r.q.start(), Services::QueueResult::OK);
    EXPECT_EQ(r.sink.sent.size(), 3u);
    EXPECT_EQ(r.q.getQueueDepth(), 0u);
}

TEST(start_is_bounded_and_reports_a_full_motor_queue) {
    Rig r;
    r.sink.capacity = 2;
    for (int i = 0; i < 4; ++i) r.q.queueCommand(r.move(i));
    r.q.arm();
    EXPECT_EQ(r.q.start(), Services::QueueResult::MOTOR_QUEUE_FULL);
    EXPECT_EQ(r.sink.sent.size(), 2u);          // two delivered
    EXPECT_EQ(r.q.getQueueDepth(), 2u);         // two still pending, not lost
    for (uint32_t t : r.sink.timeouts) EXPECT_TRUE(t <= Services::CommandQueue::FLUSH_SEND_TIMEOUT_MS);
}

TEST(estop_stops_the_driver_immediately_even_with_a_full_motor_queue) {
    Rig r;
    r.sink.capacity = 0;                         // nothing can be queued
    r.q.queueCommand(r.move(1));
    r.q.emergencyStop();
    EXPECT_EQ(r.immediate.hiZ, 1);
    EXPECT_EQ(r.q.getQueueDepth(), 0u);
    EXPECT_EQ(r.q.getState(), Services::ControllerState::ESTOP);
    EXPECT_EQ(r.sink.timeouts.back(), 0u);      // the queued HardHiZ never waits
}
