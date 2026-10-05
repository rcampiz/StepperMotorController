/**
 * @file test_smoke.cpp
 * @brief Step 0: prove the boundary headers compile on the host and that the runner works.
 */
#include "minitest.hpp"

#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "L3_services/boundary/imotion_dispatcher.hpp"
#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L5_board/boundary/ispi_bus.hpp"
#include "L1_transport/boundary/itransport.hpp"

namespace {
struct FakeMotor : Drivers::IMotorDriver {
    int runs = 0; uint32_t lastSpeed = 0;
    void init() override {}
    void run(bool, uint32_t speed) override { ++runs; lastSpeed = speed; }
    void move(bool, uint32_t) override {}
    void goTo(int32_t) override {}
    void softStop() override {} void hardStop() override {}
    void softHiZ() override {}  void hardHiZ() override {}
    void goHome() override {}   void goMark() override {} void resetPos() override {}
    uint32_t getParam(uint8_t) override { return 0; }
    void setParam(uint8_t, uint32_t) override {}
    Drivers::MotorStatus getStatus() override { return Drivers::MotorStatus{}; }
    bool isBusy() override { return false; }
    bool flagActive() override { return false; }
};
}

TEST(runner_counts_assertions) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
}

TEST(fake_driver_implements_l4_boundary) {
    FakeMotor m;
    Drivers::IMotorDriver& d = m;
    d.run(true, 1200);
    EXPECT_EQ(m.runs, 1);
    EXPECT_EQ(m.lastSpeed, 1200u);
}

TEST(null_platform_primitives_are_usable_on_host) {
    Platform::NullClock clock;
    Platform::NullLock lock;
    Platform::NullQueue<int, 4> q;
    EXPECT_EQ(clock.getTickMs(), 0u);
    lock.acquire(); lock.release();
    EXPECT_TRUE(q.send(42));
}
