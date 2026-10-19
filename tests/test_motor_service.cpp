/**
 * @file test_motor_service.cpp
 * @brief ESTOP latch: queued motion is discarded, motion is refused until CLEAR, the driver
 *        stays Hi-Z through onCommand + periodicUpdate on the Motor task.
 *
 * Runs the real MotorService + MotorController (+ sysid/trim/supervisor) over a fake driver.
 */
#include <string.h>
#include "L4_drivers/devices/powerstep01_status.hpp"
#include "L4_drivers/devices/powerstep01_units.hpp"
#include "L4_drivers/devices/powerstep01_settings.hpp"
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

/// A powerSTEP01 at register level: the real driver's settings engine (powerstep01_settings.hpp)
/// runs over this register array, so the assertions below see the chip's registers
struct FakeDriver : Drivers::IMotorDriver {
    bool hiZ = true;        // powerSTEP01 powers up in Hi-Z
    bool running = false;
    bool standby = false; int inits = 0;
    int hardHiZCalls = 0, runCalls = 0, setParams = 0, applies = 0;
    float lastRunFsps = -1.0f; bool lastRunForward = true;
    uint32_t params[32] = {};                 // powerSTEP01 registers, converted like the real driver
    Drivers::PowerStepSettings::ResetWatch watch;
    void init() override { hiZ = true; running = false; standby = false; ++inits; watch.arm(*this); }
    void setStandby(bool s) override { standby = s; if (s) { hiZ = true; running = false; } }
    void run(bool fwd, float fsps) override {
        running = true; hiZ = false; ++runCalls; lastRunFsps = fsps; lastRunForward = fwd;
        params[Drivers::PowerStepReg::SPEED] = Drivers::PowerStepUnits::fspsToSpeedReg(fsps);
    }
    void move(bool, uint32_t) override { running = true; hiZ = false; ++runCalls; }
    void goTo(int32_t) override { running = true; hiZ = false; ++runCalls; }
    void softStop() override { running = false; }
    void hardStop() override { running = false; }
    void softHiZ() override { running = false; hiZ = true; }
    void hardHiZ() override { running = false; hiZ = true; ++hardHiZCalls; }
    void goHome() override { running = true; hiZ = false; ++runCalls; }
    void goMark() override { running = true; hiZ = false; ++runCalls; }
    void resetPos() override {}
    void setMark(int32_t p) override { writeReg(Drivers::PowerStepReg::MARK, static_cast<uint32_t>(p)); }
    void setMaxSpeed(float fsps) override {
        writeReg(Drivers::PowerStepReg::MAX_SPEED, Drivers::PowerStepUnits::fspsToMaxSpeedReg(fsps));
    }
    float getMaxSpeed() override {
        return Drivers::PowerStepUnits::maxSpeedRegToFsps(params[Drivers::PowerStepReg::MAX_SPEED]);
    }
    void setAcceleration(float f) override { writeReg(Drivers::PowerStepReg::ACC, Drivers::PowerStepSettings::fsps2ToAcc(f)); }
    void setDeceleration(float f) override { writeReg(Drivers::PowerStepReg::DEC, Drivers::PowerStepSettings::fsps2ToAcc(f)); }
    const Drivers::DriverCaps& caps() const override { return Drivers::PowerStepSettings::caps(); }
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override { return Drivers::PowerStepSettings::quantize(s); }
    Drivers::ApplyResult apply(const Drivers::DriverSettings& s) override { ++applies; return Drivers::PowerStepSettings::apply(*this, s); }
    bool readSettings(Drivers::DriverSettings& s) override { s = Drivers::PowerStepSettings::readBack(*this); return true; }
    bool checkReset() override { return watch.check(*this); }
    size_t diagnostics(Harness::DriverDiagView v, char* buf, size_t n) override { return Drivers::PowerStepSettings::diagnostics(*this, v, buf, n); }
    bool toLegacyUnits(const Drivers::DriverSettings& s, Drivers::LegacyDriverUnits& u) const override { Drivers::PowerStepSettings::toLegacy(s, u); return true; }
    bool fromLegacyUnits(const Drivers::LegacyDriverUnits& u, Drivers::DriverSettings& s) const override { Drivers::PowerStepSettings::fromLegacy(u, s); return true; }
    // register port; STEP_MODE / CONFIG ignore writes with the bridges on, as the chip does
    uint32_t readReg(uint8_t reg) { return (reg < 32) ? params[reg] : 0; }
    void writeReg(uint8_t reg, uint32_t v) {
        ++setParams;
        if (!hiZ && (reg == Drivers::PowerStepReg::STEP_MODE || reg == Drivers::PowerStepReg::CONFIG)) return;
        if (reg < 32) params[reg] = v;
    }
    bool outputsOff() { return hiZ; }
    void clearFaults() {}
    Drivers::MotorStatus getStatus() override {
        Drivers::MotorStatus s;                            // no faults, not moving
        s.outputsOff = hiZ;
        return s;
    }
    Drivers::MotorStatus readState() override {
        Drivers::MotorStatus s = getStatus();
        s.positionUsteps = Drivers::PowerStepUnits::absPosToUsteps(params[Drivers::PowerStepReg::ABS_POS]);
        s.speedFsps = Drivers::PowerStepUnits::speedRegToFsps(params[Drivers::PowerStepReg::SPEED]);
        return s;
    }
    bool isBusy() override { return false; }
    bool flagActive() override { return false; }
};

/// An empty journal for the config manager (defaults on every Rig)
struct NullStore : Drivers::IConfigStore {
    size_t capacity() const override { return 4096; }
    size_t remaining() const override { return 4096; }
    bool erase() override { return true; }
    bool write(const uint8_t*, size_t) override { return true; }
    size_t read(uint8_t* out, size_t n) const override { for (size_t i = 0; i < n; ++i) out[i] = 0xFF; return n; }
};

Services::MotorCommand cmd(Services::MotorCmdType t, int32_t p1 = 0, int32_t p2 = 0) {
    Services::MotorCommand c; c.type = t; c.param1 = p1; c.param2 = p2; return c;
}

/// A clock the tests can advance (NullClock is frozen at 0)
struct StepClock : Platform::IClock {
    uint32_t now = 0;
    uint32_t getTickMs() const override { return now; }
    uint32_t getTickMsFromISR() const override { return now; }
    void delayMs(uint32_t ms) override { now += ms; }
    void sleepUntilMs(uint32_t& lastWakeMs, uint32_t periodMs) override { lastWakeMs += periodMs; now = lastWakeMs; }
};

struct Rig {
    FakeDriver drv;
    StepClock clock;
    Platform::NullLock lock;
    Platform::NullQueue<Services::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH> q;
    Platform::NullQueue<AsyncEvent, 8> events;
    Services::MotorService& svc = Services::g_motorService;

    NullStore store;

    Rig() {
        Services::g_telemetryStore.init(lock, clock);
        Services::motion.configStore.init(store, &drv);     // defaults, the fake chip's caps
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
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x07u, 4u);
}

TEST(apply_config_is_a_motor_task_job_that_writes_step_mode_in_hiz_or_via_the_safe_path) {
    Rig r;
    auto& cfg = Services::motion.stepper.config;
    const Harness::DriverSettings original = cfg.getDriverSettings();
    bool ok = false;
    // Comms side: the job is queued, nothing touches the driver until the Motor task runs
    cfg.setDriveMode(Harness::DriveMode::VOLTAGE);
    cfg.setMicrosteps(32);
    r.drv.hiZ = true;
    r.drv.params[Drivers::PowerStepReg::STEP_MODE] = 0x07;
    const int before = r.drv.setParams;
    EXPECT_FALSE(r.svc.applyConfig());                    // unanswered here: nobody drains the queue
    EXPECT_EQ(r.drv.setParams, before);
    EXPECT_EQ(r.q.available(), 1u);
    r.motorTaskPass();                                    // Hi-Z: written in place and verified
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x07u, 5u);

    // Bridges on: the driver says the resolution needs the outputs off, and the Motor task
    // takes that path itself (no second message needed)
    cfg.setMicrosteps(8);
    r.drv.hiZ = false;
    r.svc.applyConfig();
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x07u, 3u);
    EXPECT_EQ(r.drv.hardHiZCalls, 1);
    cfg.setDriverSettings(original);
}

TEST(apply_writes_the_bench_boards_registers_from_physical_settings) {
    // The bench board's saved config, now physical: TVAL 04/11/14/14 (0.31/1.33/1.56/1.56 A),
    // current mode, 1/128 asked (1/16 runs), ACC/DEC 344, MAX_SPEED 87, OCD_TH 21
    Rig r;
    auto& cfg = Services::motion.stepper.config;
    const Harness::DriverSettings original = cfg.getDriverSettings();
    Harness::DriverSettings s = original;
    s.mode = Harness::DriveMode::CURRENT;
    s.microsteps = 128;
    s.currentA = { 4 * 0.078f, 17 * 0.078f, 20 * 0.078f, 20 * 0.078f };
    s.accelFsps2 = s.decelFsps2 = 344 * 14.552f;
    s.maxSpeedFsps = 87 * (15625.0f / 1024.0f);
    s.overcurrentA = 22 * (0.03125f / 0.0012f);
    s.faultAlarms = Harness::DriverFault::OVERCURRENT | Harness::DriverFault::THERMAL_SHUTDOWN
                  | Harness::DriverFault::UNDERVOLTAGE;
    s.tuning = Harness::DriverTuning{};
    cfg.setDriverSettings(s);
    r.drv.hiZ = true;
    r.drv.params[Drivers::PowerStepReg::CONFIG] = 0x2C88;
    bool ok = false;
    r.svc.applyConfig();
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok) && ok);
    const uint32_t* p = r.drv.params;
    EXPECT_EQ(p[Drivers::PowerStepReg::KVAL_HOLD], 0x04u);
    EXPECT_EQ(p[Drivers::PowerStepReg::KVAL_RUN], 0x11u);
    EXPECT_EQ(p[Drivers::PowerStepReg::KVAL_ACC], 0x14u);
    EXPECT_EQ(p[Drivers::PowerStepReg::KVAL_DEC], 0x14u);
    EXPECT_EQ(p[Drivers::PowerStepReg::ACC], 344u);
    EXPECT_EQ(p[Drivers::PowerStepReg::DEC], 344u);
    EXPECT_EQ(p[Drivers::PowerStepReg::MAX_SPEED], 87u);
    EXPECT_EQ(p[Drivers::PowerStepReg::OCD_TH], 21u);
    EXPECT_EQ(p[Drivers::PowerStepReg::ALARM_EN], 0x0Bu);              // OCD, TH_SD, UVLO
    EXPECT_EQ(p[Drivers::PowerStepReg::FS_SPD], 0x3FFu);
    EXPECT_EQ(p[Drivers::PowerStepReg::MIN_SPEED], 0u);
    EXPECT_EQ(p[Drivers::PowerStepReg::STEP_MODE], 0xCCu);             // SYNC 8/fs, CM_VM, 1/16
    EXPECT_EQ(p[Drivers::PowerStepReg::CONFIG], 0x8000u | (11u << 10) | 0x088u);   // PRED on, TSW 48 us
    EXPECT_EQ(p[Drivers::PowerStepReg::T_FAST], 0x19u);
    // What every conversion uses: the resolution in force, from the driver's caps
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 16u);
    EXPECT_EQ(cfg.getMicrostepsPerRev(), 3200u);
    Harness::MotorDebugParams info{};
    EXPECT_FALSE(r.svc.getDebugInfo(info, Harness::DriverDiagView::SUMMARY));   // queued only
    r.motorTaskPass();
    cfg.setDriverSettings(original);
}

TEST(live_ramp_and_ceiling_commands_reach_the_driver_in_physical_units) {
    // MOT:CFG:ACCEL/DECEL/MAXSPD: whole steps/s^2 give the codes the integer path gave
    Rig r;
    r.svc.onCommand(cmd(Services::MotorCmdType::SetAccel, 5000));
    r.svc.onCommand(cmd(Services::MotorCmdType::SetDecel, 2000));
    r.svc.onCommand(cmd(Services::MotorCmdType::SetMaxSpeed, 1000));
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::ACC], 5000u * 1000u / 14552u);   // 343
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::DEC], 2000u * 1000u / 14552u);   // 137
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::MAX_SPEED], 1000u * 1024u / 15625u);
    r.svc.onCommand(cmd(Services::MotorCmdType::SetMark, -1200));
    EXPECT_EQ(Drivers::PowerStepUnits::absPosToUsteps(r.drv.params[Drivers::PowerStepReg::MARK]), -1200);
}

TEST(the_powerstep01_status_word_is_decoded_in_the_driver_only) {
    // Idle in Hi-Z, no faults: HiZ=1, BUSY=1 (active low: not moving), UVLO/OCD/STALL bits high
    Harness::MotorState idle = Drivers::decodePowerStepStatus(0xE203);
    EXPECT_TRUE(idle.outputsOff && !idle.moving && idle.faults == 0 && idle.readable);
    EXPECT_EQ(idle.raw, 0xE203u);
    // Running forward (BUSY low, DIR high, MOT_STATUS constant), overcurrent and bridge-A stall
    const uint16_t raw = static_cast<uint16_t>((1u << 4) | (3u << 5) | (1u << 9) | (1u << 15));
    Harness::MotorState run = Drivers::decodePowerStepStatus(raw);
    EXPECT_TRUE(!run.outputsOff && run.moving && run.forward);
    EXPECT_TRUE(run.phase == Harness::MotionPhase::CONSTANT);
    EXPECT_TRUE(run.ocd() && run.stalled() && !run.uvlo() && !run.thermalSD());
    EXPECT_TRUE(run.shutdownFault());
    // Thermal: TH_STATUS 1 = warning, 2 = bridge shutdown; UVLO low = undervoltage
    Harness::MotorState hot = Drivers::decodePowerStepStatus(static_cast<uint16_t>((0xE203u & ~(1u << 9)) | (2u << 11)));
    EXPECT_TRUE(hot.uvlo() && hot.thermalWarn() && hot.thermalSD());
    char names[48];
    Harness::faultNames(hot.faults, names, sizeof names);
    EXPECT_TRUE(strcmp(names, "UVLO TH_SD TH_WARN") == 0);
}

TEST(a_chip_that_reset_behind_our_back_is_initialised_again) {
    // Bench 2026-10-19: the motor supply dropped, the powerSTEP01 came back with every
    // register at its default (gate drive 4 mA, overcurrent shutdown on) and MOT:EN tripped
    // OCD. GATECFG1 is written only by init: when it changes, init runs again.
    Rig r;
    auto& ctrl = Services::motion.stepper.controller;
    r.drv.params[Drivers::PowerStepReg::GATECFG1] = 0xC3;      // what init leaves on the real chip
    ctrl.reinit();
    const int initsBefore = r.drv.inits;
    r.drv.params[Drivers::PowerStepReg::GATECFG1] = 0xC3;
    // one bad read is not a reset
    r.drv.params[Drivers::PowerStepReg::GATECFG1] = 0x00;
    r.clock.now += 600; r.motorTaskPass();
    r.drv.params[Drivers::PowerStepReg::GATECFG1] = 0xC3;
    r.clock.now += 600; r.motorTaskPass();
    EXPECT_EQ(r.drv.inits, initsBefore);
    // two in a row is: the chip is initialised again (and the config re-applied)
    r.drv.params[Drivers::PowerStepReg::GATECFG1] = 0x00;
    r.clock.now += 600; r.motorTaskPass();
    r.clock.now += 600; r.motorTaskPass();
    EXPECT_EQ(r.drv.inits, initsBefore + 1);
    EXPECT_TRUE(ctrl.chipResetsRepaired() >= 1u);
}

TEST(current_mode_writes_tval_and_the_cm_vm_bit_through_the_hiz_path) {
    Rig r;
    bool ok = false;
    auto& cfg = Services::motion.stepper.config;
    const Harness::DriverSettings original = cfg.getDriverSettings();
    // Protocol 2 aliases: chip units in, the driver translates them into physical settings
    Drivers::LegacyDriverUnits u = {};
    EXPECT_TRUE(cfg.toLegacyUnits(u));
    u.stepMode = 5;
    u.driveMode = 1;
    u.tval[0] = 0x05; u.tval[1] = 0x11; u.tval[2] = 0x12; u.tval[3] = 0x13;
    u.predEn = 1; u.tsw = 20; u.tFast = 0x2A; u.tonMin = 0; u.toffMin = 0x30;   // PRED on, TSW 84 us
    EXPECT_TRUE(cfg.setFromLegacyUnits(u));
    EXPECT_TRUE(cfg.getDriverSettings().currentA.run == 0x11 * 0.078f);
    r.drv.hiZ = true;
    r.drv.params[Drivers::PowerStepReg::STEP_MODE] = 0x05;
    r.drv.params[Drivers::PowerStepReg::CONFIG] = 0x2C88;              // chip default: PRED off, TSW 11
    r.svc.applyConfig();
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::KVAL_RUN], 0x11u);              // TVAL_RUN shares the address
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x0Fu, 0x0Cu);     // 1/32 asked: 1/16 (current-mode max) + CM_VM
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0xF0u, 0xC0u);     // SYNC out, 8 per full step
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::CONFIG], 0x8000u | (20u << 10) | 0x088u);   // low bits kept
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::T_FAST], 0x2Au);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::TON_MIN], 0x29u);              // default resolved
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::TOFF_MIN], 0x30u);
    // The cap is the driver's (caps), and the readback says what runs
    EXPECT_EQ(cfg.caps().maxMicrosteps(Harness::DriveMode::CURRENT), 16u);
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 16u);
    EXPECT_EQ(cfg.getMicrosteps(), 32u);                                          // asked, kept

    // Back to voltage mode with the bridges on: the Hi-Z path clears CM_VM and KVAL returns
    cfg.setDriveMode(Harness::DriveMode::VOLTAGE);
    r.drv.hiZ = false;
    r.svc.applyConfig();
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x0Fu, 0x05u);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::KVAL_RUN],
              static_cast<uint32_t>(Drivers::PowerStepSettings::dutyToKval(cfg.getDriverSettings().duty.run)));
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 32u);
    cfg.setDriverSettings(original);
}

TEST(step_mode_change_reads_back_the_capped_resolution) {
    // DRV:STEP_MODE 7 in current mode: 1/16 is what the chip runs, and what comes back
    Rig r;
    auto& cfg = Services::motion.stepper.config;
    const Harness::DriverSettings original = cfg.getDriverSettings();
    cfg.setDriveMode(Harness::DriveMode::CURRENT);
    r.svc.applyConfig();
    r.motorTaskPass();
    uint8_t rb = 0;
    EXPECT_FALSE(r.svc.setStepModeSafe(7, rb));         // queued
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastStepModeResult(rb));
    EXPECT_EQ(rb, 4u);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::STEP_MODE] & 0x0Fu, 0x0Cu);
    cfg.setDriverSettings(original);
}

TEST(idle_release_lets_the_coils_go_after_the_configured_time_and_motion_reenergises) {
    Rig r;
    auto& cfg = Services::motion.stepper.config;
    cfg.setIdleReleaseMs(0);
    r.drv.hiZ = false;                                   // energised, stopped
    r.drv.params[Drivers::PowerStepReg::SPEED] = 0;
    r.clock.now = 1000;
    for (int i = 0; i < 5; ++i) { r.clock.now += 1000; r.motorTaskPass(); }
    EXPECT_FALSE(r.drv.hiZ);                             // 0 = hold for ever

    cfg.setIdleReleaseMs(1500);
    r.motorTaskPass();                                   // stopped-since starts here
    r.clock.now += 1000; r.motorTaskPass();
    EXPECT_FALSE(r.drv.hiZ);                             // 1.0 s: not yet
    r.clock.now += 600; r.motorTaskPass();
    EXPECT_TRUE(r.drv.hiZ);                              // 1.6 s: released

    EXPECT_TRUE(r.q.send(cmd(Services::MotorCmdType::Run, 400, 1)));
    r.motorTaskPass();
    EXPECT_FALSE(r.drv.hiZ);                             // the chip energises on Run
    r.drv.params[Drivers::PowerStepReg::SPEED] = 100;        // moving: the timer must not run
    r.clock.now += 5000; r.motorTaskPass();
    EXPECT_FALSE(r.drv.hiZ);
    cfg.setIdleReleaseMs(0);
}

TEST(reinit_and_debug_info_run_on_the_motor_task) {
    Rig r;
    bool ok = false;
    const int inits = r.drv.inits;
    r.svc.reinit();                                       // queued only
    EXPECT_EQ(r.drv.inits, inits);
    r.motorTaskPass();
    EXPECT_EQ(r.drv.inits, inits + 1);
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);

    Harness::MotorDebugParams info{};
    r.drv.params[Drivers::PowerStepReg::KVAL_RUN] = 0x5A;
    EXPECT_FALSE(r.svc.getDebugInfo(info, Harness::DriverDiagView::REGISTERS));   // unanswered on the Comms side alone
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_TRUE(ok);

    // Paused (SPI_STOP): the job is answered "not done" at once instead of timing out
    r.svc.setDebugPaused(true);
    r.svc.reinit();
    r.motorTaskPass();
    EXPECT_TRUE(r.svc.lastJobResult(ok));
    EXPECT_FALSE(ok);
    EXPECT_EQ(r.drv.inits, inits + 1);
    r.svc.setDebugPaused(false);
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

TEST(out_of_band_estop_drops_the_pin_latches_and_clear_reinitialises_the_chip) {
    Rig r;
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 100, 1));
    EXPECT_TRUE(r.drv.running);
    const int initsBefore = r.drv.inits;

    r.svc.oobEstopFromIsr();                             // the USART2 ISR, byte 0x18
    EXPECT_TRUE(r.drv.standby);                          // one GPIO write, no SPI
    EXPECT_TRUE(r.svc.isStopLatched());
    EXPECT_TRUE(r.svc.isStandbyAsserted());
    uint32_t pin = 1, hiz = 1;
    EXPECT_TRUE(r.svc.estopTiming(pin, hiz));

    // Motor task pass: the pending stop runs, nothing else touches the driver in standby
    r.motorTaskPass();
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 100, 1));
    EXPECT_FALSE(r.drv.running);
    EXPECT_TRUE(r.drv.standby);
    EXPECT_EQ(r.drv.inits, initsBefore);

    // Clear: the Motor task releases the pin, re-initialises and re-applies the config
    r.svc.clearImmediateStop();
    r.motorTaskPass();
    EXPECT_FALSE(r.drv.standby);
    EXPECT_EQ(r.drv.inits, initsBefore + 1);
    EXPECT_FALSE(r.svc.isStandbyAsserted());
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 100, 1));
    EXPECT_TRUE(r.drv.running);                          // motion allowed again
}

TEST(a_move_speed_override_is_restored_on_soft_stop_run_and_nested_moves) {
    Rig r;
    auto& maxSpeed = r.drv.params[Drivers::PowerStepReg::MAX_SPEED];
    r.svc.onCommand(cmd(Services::MotorCmdType::SetMaxSpeed, 1038));   // full steps/s -> MAX_SPEED 68
    EXPECT_EQ(maxSpeed, 68u);

    // move at a lower speed (full steps/s; the driver converts: 107 -> MAX_SPEED 7),
    // interrupted by STOP (soft): the configured value comes back
    r.svc.onCommand(cmd(Services::MotorCmdType::Move, 6400, 107));
    EXPECT_EQ(maxSpeed, 7u);
    r.svc.onCommand(cmd(Services::MotorCmdType::SoftStop));
    EXPECT_EQ(maxSpeed, 68u);

    // a run while a move's override is active runs at the configured maximum
    r.svc.onCommand(cmd(Services::MotorCmdType::Move, 6400, 107));
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 100, 1));
    EXPECT_EQ(maxSpeed, 68u);
    r.svc.onCommand(cmd(Services::MotorCmdType::SoftStop));

    // two overrides in a row keep the original, not the first override
    r.svc.onCommand(cmd(Services::MotorCmdType::Move, 6400, 107));
    r.svc.onCommand(cmd(Services::MotorCmdType::Move, 6400, 138));   // 138 steps/s -> 9
    EXPECT_EQ(maxSpeed, 9u);
    r.svc.onCommand(cmd(Services::MotorCmdType::HardStop));
    EXPECT_EQ(maxSpeed, 68u);
}

TEST(run_reaches_the_driver_in_full_steps_per_second_and_telemetry_reads_back_physical_units) {
    // Driver boundary step 2: RUN carries steps/s to the driver, which converts; the
    // powerSTEP01 register is what L3 used to compute itself (1000 x 2^20 / 15625 = 67108)
    Rig r;
    r.svc.onCommand(cmd(Services::MotorCmdType::Run, 1000, 0));
    EXPECT_TRUE(r.drv.lastRunFsps == 1000.0f);
    EXPECT_FALSE(r.drv.lastRunForward);
    EXPECT_EQ(r.drv.params[Drivers::PowerStepReg::SPEED], 67108u);

    r.drv.params[Drivers::PowerStepReg::ABS_POS] = 0x3FFF00u;          // -256 microsteps
    r.motorTaskPass();
    Protocol::TelemetrySnapshot t = Services::g_telemetryStore.getSnapshot();
    EXPECT_EQ(t.motor.position, -256);
    EXPECT_EQ(t.motor.speed, 999u);                                  // as the raw path reported it

    // Standby (out-of-band stop): speed 0, position as last read
    r.svc.oobEstopFromIsr();
    r.motorTaskPass();
    t = Services::g_telemetryStore.getSnapshot();
    EXPECT_EQ(t.motor.speed, 0u);
    EXPECT_EQ(t.motor.position, -256);
    EXPECT_FALSE(t.motor.statusReadable);
    r.svc.clearImmediateStop();
    r.motorTaskPass();
    r.drv.params[Drivers::PowerStepReg::ABS_POS] = 0;
    r.drv.params[Drivers::PowerStepReg::SPEED] = 0;
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
