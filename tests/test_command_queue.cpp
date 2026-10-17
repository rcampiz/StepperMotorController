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
    int hiZ = 0, clears = 0; bool latched = false;
    void immediateHardHiZ() override { ++hiZ; latched = true; }
    void clearImmediateStop() override { ++clears; latched = false; }
    bool isStopLatched() const override { return latched; }
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

TEST(the_latch_alone_reports_estop_and_clear_releases_it) {
    // Phase 4 step 4: the out-of-band byte latches the MotorService without going through the
    // queue; the queue's state must still read ESTOP, refuse motion, and clear normally.
    Rig r;
    r.immediate.latched = true;
    EXPECT_EQ(r.q.getState(), Services::ControllerState::ESTOP);
    EXPECT_EQ(r.q.clearFault(), Services::QueueResult::OK);
    EXPECT_EQ(r.immediate.clears, 1);
    EXPECT_EQ(r.q.getState(), Services::ControllerState::IDLE);
}

TEST(clear_after_estop_releases_the_latch_exactly_once) {
    Rig r;
    r.q.emergencyStop();
    EXPECT_TRUE(r.immediate.latched);
    EXPECT_EQ(r.q.clearFault(), Services::QueueResult::OK);
    EXPECT_EQ(r.immediate.clears, 1);
    EXPECT_FALSE(r.immediate.latched);
    EXPECT_EQ(r.q.clearFault(), Services::QueueResult::INVALID_STATE);   // already IDLE
    EXPECT_EQ(r.immediate.clears, 1);
}

// --- SYNC:START:AT is a message to the Motor task, not a wait on the Comms task ----------
TEST(start_at_schedules_a_motor_task_message_and_stays_armed) {
    Rig r;
    r.q.queueCommand(r.move(100));
    r.q.arm();
    EXPECT_EQ(r.q.startAt(1000), Services::QueueResult::OK);          // tick 1000 us, now 0
    EXPECT_EQ(r.q.getState(), Services::ControllerState::ARMED);        // nothing flushed yet
    EXPECT_TRUE(r.q.scheduledStartPending());
    EXPECT_EQ(r.sink.sent.size(), 1u);
    EXPECT_TRUE(r.sink.sent[0].type == Services::MotorCmdType::StartAt);
    EXPECT_EQ(static_cast<uint32_t>(r.sink.sent[0].param1), 1000u);

    // Motor task side, at the tick
    EXPECT_TRUE(r.q.executeScheduledStart());
    EXPECT_EQ(r.q.getState(), Services::ControllerState::RUNNING);
    EXPECT_FALSE(r.q.scheduledStartPending());
    EXPECT_EQ(r.sink.sent.size(), 2u);
    EXPECT_TRUE(r.sink.sent[1].type == Services::MotorCmdType::Move);
}

TEST(start_at_rejects_a_past_tick_and_a_far_future_tick_without_scheduling) {
    Rig r;
    r.q.queueCommand(r.move(100));
    r.q.arm();
    EXPECT_EQ(r.q.startAt(0xFFFFFFF0u), Services::QueueResult::TIMEOUT_EXPIRED);   // -16 us
    EXPECT_EQ(r.q.startAt(Services::CommandQueue::MAX_START_AT_US + 1), Services::QueueResult::INVALID_PARAM);
    EXPECT_FALSE(r.q.scheduledStartPending());
    EXPECT_EQ(r.sink.sent.size(), 0u);
}

TEST(a_cleared_or_estopped_queue_cancels_the_scheduled_start) {
    Rig r;
    r.q.queueCommand(r.move(100));
    r.q.arm();
    r.q.startAt(500);
    r.q.clearQueue();
    EXPECT_FALSE(r.q.scheduledStartPending());
    EXPECT_FALSE(r.q.executeScheduledStart());                           // nothing to flush
    EXPECT_EQ(r.sink.sent.size(), 1u);                                   // only the StartAt

    r.q.queueCommand(r.move(100));
    r.q.arm();
    r.q.startAt(500);
    r.q.emergencyStop();
    EXPECT_FALSE(r.q.scheduledStartPending());
    EXPECT_FALSE(r.q.executeScheduledStart());
    EXPECT_EQ(r.q.getState(), Services::ControllerState::ESTOP);
}
