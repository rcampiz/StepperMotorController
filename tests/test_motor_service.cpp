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
#include "F_platform/boundary/ischeduler.hpp"
#include "harness/pins/async_event.hpp"
#include <vector>
#include <string>

namespace {

struct FakeDriver : Drivers::IMotorDriver {
    bool hiZ = true;        // powerSTEP01 powers up in Hi-Z
    bool running = false;
    int hardHiZCalls = 0, runCalls = 0, setParams = 0;
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
    uint32_t getParam(uint8_t) override { return 0; }
    void setParam(uint8_t, uint32_t) override { ++setParams; }
    Drivers::MotorStatus getStatus() override {
        Drivers::MotorStatus s; s.raw = 0xFFFE;            // no faults, not busy (bit1 set)
        if (hiZ) s.raw |= 1u; else s.raw &= ~1u;
        return s;
    }
    bool isBusy() override { return false; }
    bool flagActive() override { return false; }
};

struct FakeScheduler : Platform::IScheduler {
    Platform::TaskHandle createTask(const Platform::TaskDescriptor&) override { return nullptr; }
    [[noreturn]] void start() override { for (;;) {} }
    void suspendTask(Platform::TaskHandle) override {}
    void resumeTask(Platform::TaskHandle) override {}
};

Services::MotorCommand cmd(Services::MotorCmdType t, int32_t p1 = 0, int32_t p2 = 0) {
    Services::MotorCommand c; c.type = t; c.param1 = p1; c.param2 = p2; return c;
}

struct Rig {
    FakeDriver drv;
    Platform::NullClock clock;
    Platform::NullLock lock;
    FakeScheduler sched;
    Platform::NullQueue<Services::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH> q;
    Platform::NullQueue<AsyncEvent, 8> events;
    Services::MotorService& svc = Services::g_motorService;

    Rig() {
        Services::g_telemetryStore.init(lock, clock);
        Services::motion.control.mode.init(lock);
        Services::motion.stepper.event.init(events);
        Services::motion.stepper.controller.bind(drv, clock);
        svc.init(Services::motion.stepper.controller, Services::motion.stepper.controller,
                 q, clock, sched, nullptr, Services::g_telemetryStore);
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
