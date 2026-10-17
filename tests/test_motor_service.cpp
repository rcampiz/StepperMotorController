/**
 * @file test_motor_service.cpp
 * @brief ESTOP latch: queued motion is discarded, motion is refused until CLEAR, the driver
 *        stays Hi-Z through onCommand + periodicUpdate on the Motor task.
 *
 * Runs the real MotorService + MotorController (+ sysid/trim/supervisor) over a fake driver.
 */
#include "minitest.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "harness/pins/async_event.hpp"
#include <vector>
#include <string>

namespace {

struct FakeDriver : Drivers::IMotorDriver {
    bool hiZ = true;        // powerSTEP01 powers up in Hi-Z
    bool running = false;
    int hardHiZCalls = 0, runCalls = 0, setParams = 0;
    uint32_t params[32] = {};
    void init() override { hiZ = true; running = false; }
    void run(bool, uint32_t) override { running = true; hiZ = false; ++runCalls; }
    void move(bool, uint32_t) override { running = true; hiZ = false; ++runCalls; }
    void goTo(int32_t) override { running = true; hiZ = false; ++runCalls; }
    void softStop() override { running = false; }
    void hardStop() override { running = false; }
    void softHiZ() override { running = false; hiZ = true; }
    void hardHiZ() override { running = false; hiZ = true; ++hardHiZCalls; }
    void goHome() override { running = true; hiZ = false; ++runCalls; }
    void goMark() override { running = true; hiZ = false; ++runCalls; }
    void resetPos() override {}
    uint32_t getParam(uint8_t reg) override { return (reg < 32) ? params[reg] : 0; }
    void setParam(uint8_t reg, uint32_t v) override { ++setParams; if (reg < 32) params[reg] = v; }
    Drivers::MotorStatus getStatus() override {
        Drivers::MotorStatus s; s.raw = 0xFFFE;            // no faults, not busy (bit1 set)
        if (hiZ) s.raw |= 1u; else s.raw &= ~1u;
        return s;
    }
    bool isBusy() override { return false; }
    bool flagActive() override { return false; }
};

Services::MotorCommand cmd(Services::MotorCmdType t, int32_t p1 = 0, int32_t p2 = 0) {
    Services::MotorCommand c; c.type = t; c.param1 = p1; c.param2 = p2; return c;
}

struct Rig {
    FakeDriver drv;
    Platform::NullClock clock;
    Platform::NullLock lock;
    Platform::NullQueue<Services::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH> q;
    Platform::NullQueue<AsyncEvent, 8> events;
    Services::MotorService& svc = Services::g_motorService;

    Rig() {
        Services::g_telemetryStore.init(lock, clock);
        Services::motion.control.mode.init(lock);
        Services::motion.stepper.event.init(events);
        Services::motion.stepper.controller.bind(drv, clock);
        svc.init(Services::motion.stepper.controller, Services::motion.stepper.controller,
                 q, clock, nullptr, Services::g_telemetryStore);
        // g_motorService is a process-wide singleton: start every test unlatched and idle
        svc.clearImmediateStop();
        motorTaskPass();
        drv.hardHiZCalls = 0; drv.runCalls = 0;
    }

    /// One Motor-task pass: drain the queue through onCommand, then periodicUpdate.
    void motorTaskPass() {
        Services::MotorCommand c;
        while (q.receive(c)) svc.onCommand(c);
        svc.periodicUpdate();
    }
};

} // namespace

TEST(estop_discards_queued_motion_and_the_driver_stays_hiz) {
    Rig r;
    EXPECT_TRUE(r.q.send(cmd(Services::MotorCmdType::Run, 500, 1)));
    EXPECT_TRUE(r.q.send(cmd(Services::MotorCmdType::Move, 1000, 0)));
    EXPECT_EQ(r.q.available(), 2u);

    r.svc.immediateHardHiZ();                      // Comms task

    EXPECT_EQ(r.q.available(), 0u);                // queued RUN/MOVE gone
    EXPECT_TRUE(r.drv.hiZ);
    EXPECT_EQ(r.drv.hardHiZCalls, 1);
    EXPECT_TRUE(r.svc.isStopLatched());

    r.motorTaskPass();                             // Motor task: onCommand(none) + periodicUpdate
    EXPECT_FALSE(r.drv.running);
    EXPECT_TRUE(r.drv.hiZ);
    EXPECT_EQ(r.drv.runCalls, 0);
    EXPECT_TRUE(Services::motion.stepper.controller.motionInhibited());
    EXPECT_TRUE(r.drv.hardHiZCalls >= 2);          // re-asserted from the Motor task
}

TEST(motion_commands_are_refused_while_latched_but_config_passes) {
    Rig r;
    r.svc.immediateHardHiZ();
    const uint32_t refusedBefore = r.svc.refusedWhileLatched();
    const int paramsBefore = r.drv.setParams;

    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    r.svc.onCommand(cmd(Services::MotorCmdType::GoTo, 42, 0));
    r.svc.periodicUpdate();
    EXPECT_FALSE(r.drv.running);
    EXPECT_TRUE(r.drv.hiZ);
    EXPECT_EQ(r.svc.refusedWhileLatched(), refusedBefore + 2);

    r.svc.onCommand(cmd(Services::MotorCmdType::SetMaxSpeed, 300, 0));
    EXPECT_TRUE(r.drv.setParams > paramsBefore);   // non-motion commands still reach the driver
    EXPECT_TRUE(r.drv.hiZ);
}

TEST(clear_re_enables_motion_on_the_next_motor_task_pass) {
    Rig r;
    r.svc.immediateHardHiZ();
    r.motorTaskPass();
    EXPECT_TRUE(Services::motion.stepper.controller.motionInhibited());

    r.svc.clearImmediateStop();                    // Comms task (CLEAR)
    EXPECT_FALSE(r.svc.isStopLatched());
    r.motorTaskPass();
    EXPECT_FALSE(Services::motion.stepper.controller.motionInhibited());

    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    EXPECT_TRUE(r.drv.running);
    EXPECT_FALSE(r.drv.hiZ);
}

TEST(a_second_estop_after_clear_latches_again) {
    Rig r;
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    EXPECT_TRUE(r.drv.running);
    r.svc.immediateHardHiZ();
    r.motorTaskPass();
    EXPECT_TRUE(r.drv.hiZ);
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    EXPECT_TRUE(r.drv.hiZ);
}

TEST(step_mode_is_a_message_answered_by_the_motor_task) {
    Rig r;
    uint8_t rb = 0;
    // Comms task side: queues SetStepMode; nobody drains the queue here, so it times out
    EXPECT_FALSE(r.svc.setStepModeSafe(4, rb));
    EXPECT_EQ(rb, 0xFF);
    EXPECT_EQ(r.q.available(), 1u);

    // Motor task side: executes the rewrite and publishes the readback
    r.motorTaskPass();
    EXPECT_EQ(r.q.available(), 0u);
    uint8_t answered = 0;
    EXPECT_TRUE(r.svc.lastStepModeResult(answered));
    EXPECT_EQ(answered, 4);
    EXPECT_EQ(r.drv.params[Drivers::MotorReg::STEP_MODE] & 0x07u, 4u);
}

TEST(apply_config_writes_step_mode_only_in_hiz_else_via_the_motor_task) {
    Rig r;
    const uint8_t original = Services::motion.stepper.config.getStepMode();
    // Hi-Z (boot, reinit, output disabled): written in place and verified
    Services::motion.stepper.config.setStepMode(5);
    r.drv.hiZ = true;
    r.drv.params[Drivers::MotorReg::STEP_MODE] = 0x07;
    EXPECT_TRUE(r.svc.applyConfig());
    EXPECT_EQ(r.drv.params[Drivers::MotorReg::STEP_MODE] & 0x07u, 5u);

    // Bridges on: the chip would ignore the write, so nothing is written in place and a
    // SetStepMode message is queued for the Motor task (times out here: nobody drains it)
    Services::motion.stepper.config.setStepMode(3);
    r.drv.hiZ = false;
    EXPECT_FALSE(r.svc.applyConfig());
    EXPECT_EQ(r.drv.params[Drivers::MotorReg::STEP_MODE] & 0x07u, 5u);
    EXPECT_EQ(r.q.available(), 1u);
    r.motorTaskPass();                                    // Hi-Z, write, verify, SoftStop
    EXPECT_EQ(r.drv.params[Drivers::MotorReg::STEP_MODE] & 0x07u, 3u);
    EXPECT_EQ(r.drv.hardHiZCalls, 1);

    // Already matching: no write at all, whatever the bridge state
    const int before = r.drv.setParams;
    EXPECT_TRUE(r.svc.applyConfig());
    EXPECT_EQ(r.drv.params[Drivers::MotorReg::STEP_MODE] & 0x07u, 3u);
    EXPECT_TRUE(r.drv.setParams > before);                // the other registers are written
    Services::motion.stepper.config.setStepMode(original);
}

namespace {
struct FakeScheduledStart : Services::IScheduledStart {
    bool pending = true; int executed = 0;
    bool scheduledStartPending() const override { return pending; }
    bool executeScheduledStart() override { ++executed; pending = false; return true; }
};
}

TEST(start_at_on_the_motor_task_flushes_at_the_tick_unless_latched) {
    Rig r;
    FakeScheduledStart sched;
    r.svc.setScheduledStart(&sched);
    // The host TickTimer reads 0: a tick of 0 is "now", so the Motor task flushes at once
    r.svc.onCommand(cmd(Services::MotorCmdType::StartAt, 0));
    EXPECT_EQ(sched.executed, 1);
    EXPECT_FALSE(sched.pending);

    // Latched (ESTOP written from the Comms task): the scheduled start is abandoned
    sched.pending = true;
    r.svc.immediateHardHiZ();
    r.svc.onCommand(cmd(Services::MotorCmdType::StartAt, 0));
    EXPECT_EQ(sched.executed, 1);
    r.svc.clearImmediateStop();
    r.motorTaskPass();
}

TEST(debug_pause_stops_all_driver_traffic_until_resumed) {
    Rig r;
    r.svc.setDebugPaused(true);
    const int hiZBefore = r.drv.hardHiZCalls, runBefore = r.drv.runCalls;
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    r.svc.periodicUpdate();
    EXPECT_EQ(r.drv.runCalls, runBefore);
    EXPECT_EQ(r.drv.hardHiZCalls, hiZBefore);
    r.svc.setDebugPaused(false);
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 500, 1));
    EXPECT_TRUE(r.drv.running);
}
