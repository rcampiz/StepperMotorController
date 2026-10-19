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
    int runs = 0; float lastSpeed = 0.0f;
    void init() override {}
    void run(bool, float fullStepsPerSec) override { ++runs; lastSpeed = fullStepsPerSec; }
    void move(bool, uint32_t) override {}
    void goTo(int32_t) override {}
    void softStop() override {} void hardStop() override {}
    void softHiZ() override {}  void hardHiZ() override {}
    void goHome() override {}   void goMark() override {} void resetPos() override {}
    void setMaxSpeed(float) override {}
    float getMaxSpeed() override { return 0.0f; }
    void setMark(int32_t) override {}
    void setAcceleration(float) override {}
    void setDeceleration(float) override {}
    // A chip-neutral driver: its own caps, settings taken as they come, no protocol 2 units
    const Drivers::DriverCaps& caps() const override { static const Drivers::DriverCaps c = {}; return c; }
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override { return s; }
    Drivers::ApplyResult apply(const Drivers::DriverSettings&) override { return Drivers::ApplyResult::APPLIED; }
    bool readSettings(Drivers::DriverSettings&) override { return false; }
    Drivers::MotorStatus getStatus() override { return Drivers::MotorStatus{}; }
    Drivers::MotorStatus readState() override { return Drivers::MotorStatus{}; }
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
    d.run(true, 1200.0f);                      // full steps/s
    EXPECT_EQ(m.runs, 1);
    EXPECT_TRUE(m.lastSpeed == 1200.0f);
    // The boundary's defaults: no reset watch, no diagnostics, no protocol 2 chip units
    Drivers::LegacyDriverUnits u = {};
    Drivers::DriverSettings s = {};
    EXPECT_FALSE(d.checkReset());
    EXPECT_FALSE(d.toLegacyUnits(s, u));
    EXPECT_FALSE(d.fromLegacyUnits(u, s));
    char text[8] = "x";
    EXPECT_EQ(d.diagnostics(Harness::DriverDiagView::REGISTERS, text, sizeof text), 0u);
    EXPECT_EQ(text[0], '\0');
}

TEST(null_platform_primitives_are_usable_on_host) {
    Platform::NullClock clock;
    Platform::NullLock lock;
    Platform::NullQueue<int, 4> q;
    EXPECT_EQ(clock.getTickMs(), 0u);
    lock.acquire(); lock.release();
    EXPECT_TRUE(q.send(42));
}
