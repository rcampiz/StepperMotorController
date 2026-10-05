/**
 * @file test_telemetry_store.cpp
 * @brief Step 1 (store moved to L3 in step 7): motor telemetry comes from explicit service writes.
 *
 * Exercises the real Services::TelemetryStore store with Null platform primitives: the writes that
 * MotorController::periodicUpdate now performs must surface as converted motor telemetry.
 */
#include "minitest.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "L4_drivers/boundary/imotor_driver.hpp"

namespace {
struct CountingLock : Platform::ILock {
    int acquires = 0, releases = 0;
    void acquire() override { ++acquires; }
    void release() override { ++releases; }
};
struct FixedClock : Platform::NullClock {
    uint32_t now = 1234;
    uint32_t getTickMs() const override { return now; }
};
}

TEST(motor_writes_surface_as_converted_telemetry) {
    CountingLock lock; FixedClock clock;
    Services::TelemetryStore snap;
    EXPECT_TRUE(snap.init(lock, clock));

    // What MotorController::periodicUpdate writes after polling the driver
    const uint16_t statusBusyLow = 0x0000;         // powerSTEP01 BUSY is active-low: 0 => busy
    snap.writeMotorStatus(statusBusyLow);
    snap.writeMotorParam(Drivers::MotorReg::ABS_POS, 0x3FFFFFu);   // 22-bit two's complement -1
    snap.writeMotorParam(Drivers::MotorReg::SPEED, 0);

    Protocol::TelemetrySnapshot t = snap.getSnapshot();
    EXPECT_EQ(t.motor.statusReg, statusBusyLow);
    EXPECT_EQ(t.motor.position, -1);
    EXPECT_EQ(t.motor.speed, 0u);
    EXPECT_EQ(t.timestamp, 1234u);
}

TEST(unrelated_params_do_not_touch_motor_telemetry) {
    CountingLock lock; FixedClock clock;
    Services::TelemetryStore snap; snap.init(lock, clock);
    snap.writeMotorParam(Drivers::MotorReg::ABS_POS, 100);
    snap.writeMotorParam(Drivers::MotorReg::MAX_SPEED, 0x41);       // not position/speed
    EXPECT_EQ(snap.getSnapshot().motor.position, 100);
}

TEST(every_write_and_read_is_lock_balanced) {
    CountingLock lock; FixedClock clock;
    Services::TelemetryStore snap; snap.init(lock, clock);
    snap.writeMotorStatus(1);
    snap.writeMotorParam(Drivers::MotorReg::SPEED, 2);
    Protocol::SystemTelemetry sys{}; sys.uptimeTicks = 7; snap.writeSystem(sys);
    (void)snap.getSnapshot();
    EXPECT_EQ(lock.acquires, lock.releases);
    EXPECT_EQ(lock.acquires, 4);
}
