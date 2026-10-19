/**
 * @file test_powerstep01.cpp
 * @brief Step 3: the L4 powerSTEP01 driver runs on bare Board:: pins (no adapters).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "L4_drivers/devices/powerstep01.hpp"
#include "L4_drivers/devices/powerstep01_settings.hpp"
#include <cmath>
#include <limits>
#include <cstring>
#include <string>

using namespace fakes;

namespace {
struct Rig {
    FakeSPIBus spi; FakeGPIO cs, stbyRst, busy, flag;
    Drivers::PowerSTEP01 motor{spi, cs, stbyRst, busy, flag};
};
}

TEST(construction_deasserts_cs_and_holds_standby) {
    Rig r;
    EXPECT_TRUE(r.cs.level);          // CS idle high
    EXPECT_FALSE(r.stbyRst.level);    // STBY/RESET held low = standby
    EXPECT_TRUE(r.spi.tx.empty());    // nothing on the bus yet
}

TEST(run_sends_command_byte_and_20bit_speed_msb_first) {
    Rig r;
    r.motor.runRaw(true, 0x12345);
    const std::vector<uint8_t> expected{0x51, 0x01, 0x23, 0x45};   // RUN|fwd, speed[19:16], [15:8], [7:0]
    EXPECT_TRUE(r.spi.tx == expected);
    EXPECT_EQ(r.spi.mode, Board::SPIMode::Mode3);
    EXPECT_EQ(r.spi.prescaler, Board::SPIPrescaler::Div32);
    EXPECT_EQ(r.spi.locks, 1);
    EXPECT_EQ(r.spi.unlocks, 1);
}

// ---- Driver boundary step 2: the chip's units live here, the interface is physical ----

namespace PU = Drivers::PowerStepUnits;

/// What L3/L2 computed before step 2: raw = sps x 2^20 / 15625 (integer)
static uint32_t oldSpeedRaw(uint32_t sps) {
    return static_cast<uint32_t>((static_cast<uint64_t>(sps) * 1048576ULL) / 15625ULL);
}

TEST(run_takes_full_steps_per_second_and_sends_the_speed_register) {
    Rig r;
    r.motor.run(false, 1000.0f);                   // 1000 x 2^20 / 15625 = 67108.86 -> 67108 = 0x010624
    const std::vector<uint8_t> expected{0x50, 0x01, 0x06, 0x24};
    EXPECT_TRUE(r.spi.tx == expected);
}

TEST(speed_register_conversion_matches_the_old_integer_path_for_every_whole_speed) {
    int mismatches = 0;
    for (uint32_t sps = 0; sps < 15625; ++sps) {
        if (PU::fspsToSpeedReg(static_cast<float>(sps)) != oldSpeedRaw(sps)) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0);
    // 15625 encodes to 2^20, which the 20-bit field cannot hold (the old code sent 0): saturate
    EXPECT_EQ(PU::fspsToSpeedReg(15625.0f), 0xFFFFFu);
    EXPECT_EQ(PU::fspsToSpeedReg(1e9f), 0xFFFFFu);
    EXPECT_EQ(PU::fspsToSpeedReg(0.0f), 0u);
    EXPECT_EQ(PU::fspsToSpeedReg(-5.0f), 0u);
    EXPECT_EQ(PU::fspsToSpeedReg(std::numeric_limits<float>::quiet_NaN()), 0u);
    EXPECT_EQ(PU::fspsToSpeedReg(0.0149f), 0u);                        // below one LSB
    EXPECT_EQ(PU::fspsToSpeedReg(0.0150f), 1u);                        // one LSB = 0.0149012 steps/s
    EXPECT_EQ(PU::fspsToSpeedReg(1012.5f), 67947u);                    // fractions truncate
}

TEST(speed_readback_keeps_the_integer_part_exact_for_every_register_value) {
    int badFloor = 0, badClose = 0, badSign = 0;
    for (uint32_t reg = 0; reg <= 0xFFFFFu; ++reg) {
        const float f = PU::speedRegToFsps(reg);
        const uint64_t n = static_cast<uint64_t>(reg) * 15625u;
        if (static_cast<uint32_t>(f) != static_cast<uint32_t>(n >> 20)) ++badFloor;   // telemetry speed
        const double exact = static_cast<double>(n) / 1048576.0;
        if (std::fabs(static_cast<double>(f) - exact) > 1.1e-3) ++badClose;   // within ~1 float step
        if ((f > 0.0f) != (reg > 0u)) ++badSign;                                      // idle release
    }
    EXPECT_EQ(badFloor, 0);
    EXPECT_EQ(badClose, 0);
    EXPECT_EQ(badSign, 0);
    EXPECT_EQ(static_cast<uint32_t>(PU::speedRegToFsps(67108u)), 999u);   // RUN 1000 reads 999 (as always)
}

TEST(max_speed_round_trips_and_matches_the_old_override_for_every_whole_speed) {
    int badRoundTrip = 0, badOverride = 0;
    for (uint32_t reg = 0; reg <= 0x3FFu; ++reg) {
        if (PU::fspsToMaxSpeedReg(PU::maxSpeedRegToFsps(reg)) != reg) ++badRoundTrip;   // save/restore
    }
    for (uint32_t sps = 1; sps <= 15625; ++sps) {
        uint32_t old = sps * 1024u / 15625u;                       // motion_service::speedOverrideRaw
        if (old < 1u) old = 1u;
        if (old > 1023u) old = 1023u;
        if (PU::fspsToMaxSpeedReg(static_cast<float>(sps)) != old) ++badOverride;
    }
    EXPECT_EQ(badRoundTrip, 0);
    EXPECT_EQ(badOverride, 0);
    EXPECT_EQ(PU::fspsToMaxSpeedReg(0.0f), 0u);
    EXPECT_EQ(PU::fspsToMaxSpeedReg(3.0f), 1u);             // a positive speed never becomes 0
    EXPECT_TRUE(PU::maxSpeedRegToFsps(0x41u) == 991.8212890625f);   // 65 x 15625 / 1024, exact
}

TEST(abs_pos_is_sign_extended_from_22_bits) {
    EXPECT_EQ(PU::absPosToUsteps(0u), 0);
    EXPECT_EQ(PU::absPosToUsteps(0x3FFFFFu), -1);
    EXPECT_EQ(PU::absPosToUsteps(0x200000u), -2097152);
    EXPECT_EQ(PU::absPosToUsteps(0x1FFFFFu), 2097151);
    EXPECT_EQ(PU::absPosToUsteps(0xFFC00005u), 5);          // bits above 21 ignored
}

TEST(read_state_polls_status_position_and_speed_and_converts_them) {
    Rig r;
    // STATUS 0xE203 (Hi-Z, idle), ABS_POS 0x3FFFFE (-2), SPEED 0x010624 (67108: 999.99 steps/s)
    r.spi.rxScript = {0x00, 0xE2, 0x03, 0x00, 0x3F, 0xFF, 0xFE, 0x00, 0x01, 0x06, 0x24};
    const Drivers::MotorStatus s = r.motor.readState();
    const std::vector<uint8_t> expected{0xD0, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00};
    EXPECT_TRUE(r.spi.tx == expected);                       // GetStatus, GetParam ABS_POS, GetParam SPEED
    EXPECT_TRUE(s.outputsOff && !s.moving);
    EXPECT_EQ(s.positionUsteps, -2);
    EXPECT_EQ(static_cast<uint32_t>(s.speedFsps), 999u);
    EXPECT_TRUE(s.speedFsps > 999.98f && s.speedFsps < 999.99f);
}

TEST(set_max_speed_takes_full_steps_per_second) {
    Rig r;
    r.motor.setMaxSpeed(107.0f);                             // 107 x 1024 / 15625 = 7.01 -> 7
    const std::vector<uint8_t> expected{0x07, 0x00, 0x07};   // SetParam MAX_SPEED, 2 bytes
    EXPECT_TRUE(r.spi.tx == expected);
    r.spi.tx.clear();
    r.spi.rxScript = {0x00, 0x00, 0x41};
    EXPECT_TRUE(r.motor.getMaxSpeed() == PU::maxSpeedRegToFsps(0x41u));
}

TEST(move_reverse_masks_steps_to_22_bits) {
    Rig r;
    r.motor.move(false, 0xFFFFFFFFu);
    const std::vector<uint8_t> expected{0x40, 0x3F, 0xFF, 0xFF};
    EXPECT_TRUE(r.spi.tx == expected);
}

TEST(every_byte_toggles_chip_select) {
    Rig r;
    int before = r.cs.writes;
    r.motor.hardHiZ();                 // one command byte
    EXPECT_EQ(r.cs.writes - before, 2); // low then high
    EXPECT_TRUE(r.cs.level);
}

// ---------------------------------------------------------------------------------------
// Driver boundary step 3: settings in physical units, converted by the driver
// ---------------------------------------------------------------------------------------
namespace PS = Drivers::PowerStepSettings;

TEST(acceleration_codes_round_trip_and_whole_requests_match_the_old_integer_path) {
    // Every code survives code -> full steps/s^2 (stored as a float) -> code
    for (uint32_t c = 0; c <= 0xFFFu; ++c) {
        EXPECT_EQ(static_cast<uint32_t>(PS::fsps2ToAcc(PS::accToFsps2(c))), c == 0 ? 0u : c);
    }
    // Every whole request gives what MOT:CFG:ACCEL always wrote: n x 1000 / 14552, at least 1
    bool allSame = true;
    for (uint32_t n = 1; n <= 59590u; ++n) {
        uint32_t old = n * 1000u / 14552u;
        if (old < 1u) old = 1u;
        if (PS::fsps2ToAcc(static_cast<float>(n)) != old) { allSame = false; break; }
    }
    EXPECT_TRUE(allSame);
    EXPECT_EQ(PS::fsps2ToAcc(0.0f), 0u);
    EXPECT_EQ(PS::fsps2ToAcc(1.0e9f), 0xFFFu);
}

TEST(level_and_threshold_codes_round_trip_through_physical_units) {
    for (uint32_t c = 0; c <= 0x7Fu; ++c) EXPECT_EQ(PS::ampsToTval(PS::tvalToAmps(c)), c);
    for (uint32_t c = 0; c <= 0xFFu; ++c) EXPECT_EQ(PS::dutyToKval(PS::kvalToDuty(c)), c);
    for (uint32_t c = 0; c <= 0x1Fu; ++c) EXPECT_EQ(PS::ampsToOcd(PS::ocdToAmps(c)), c);
    for (uint32_t c = 0; c <= 0x1Fu; ++c) EXPECT_EQ(PS::stallToCode(PS::codeToStall(c)), c);
    // OCD_TH is a drain-source voltage: (code + 1) x 31.25 mV; the amps are an estimate from
    // the IHM03A1's ~1.2 mOhm MOSFETs, so even code 0 is ~26 A
    EXPECT_TRUE(std::fabs(PS::ocdToAmps(21) * PS::BOARD_RDS_ON_OHM - 0.6875f) < 1e-5f);
    EXPECT_TRUE(PS::ocdToAmps(0) > 25.0f);
    const Drivers::DriverCaps& c = PS::caps();
    EXPECT_TRUE(c.overcurrentOhms > 0.0f);                       // displays show volts and the estimate
    EXPECT_EQ(PS::ampsToTval(1.33f), 0x11u);                       // the GUI's 1.33 A -> 17 x 78 mA
    EXPECT_EQ(PS::ampsToTval(100.0f), 0x7Fu);
}

TEST(quantize_is_idempotent_and_caps_the_resolution_by_drive_mode) {
    Drivers::DriverSettings s = {};
    s.microsteps = 128;
    s.mode = Drivers::DriveMode::CURRENT;
    s.currentA = { 0.3f, 1.3f, 1.5f, 1.6f };
    s.duty = { 0.1f, 0.2f, 0.3f, 0.4f };
    s.accelFsps2 = 5000.0f; s.decelFsps2 = 2000.0f; s.maxSpeedFsps = 1000.0f;
    s.overcurrentA = 300.0f; s.stallThreshold01 = 0.5f;
    s.faultAlarms = 0xFF;
    const Drivers::DriverSettings q = PS::quantize(s);
    EXPECT_EQ(q.microsteps, 16u);
    EXPECT_EQ(q.faultAlarms, 0x3Fu);
    const Drivers::DriverSettings qq = PS::quantize(q);
    EXPECT_TRUE(std::memcmp(&q, &qq, sizeof(q)) == 0);
    s.mode = Drivers::DriveMode::VOLTAGE;
    EXPECT_EQ(PS::quantize(s).microsteps, 128u);
    EXPECT_EQ(PS::caps().maxMicrosteps(Drivers::DriveMode::CURRENT), 16u);
    EXPECT_EQ(PS::caps().maxMicrosteps(Drivers::DriveMode::VOLTAGE), 128u);
}

TEST(protocol_2_chip_units_round_trip_through_the_physical_settings) {
    Drivers::LegacyDriverUnits u = {};
    u.kval[0] = 0x20; u.kval[1] = 0x30; u.kval[2] = 0x40; u.kval[3] = 0xFF;
    u.tval[0] = 0x04; u.tval[1] = 0x11; u.tval[2] = 0x14; u.tval[3] = 0x7F;
    u.driveMode = 1; u.stepMode = 7; u.ocdTh = 21; u.stallTh = 31;
    u.acc = 344; u.dec = 4095; u.maxSpeed = 87; u.minSpeed = 12; u.fsSpd = 0x27;
    u.predEn = 0; u.tsw = 20; u.tFast = 0x2A; u.tonMin = 0x10; u.toffMin = 0x30;
    for (uint8_t alarms : { 0x00, 0x01, 0x10, 0x20, 0x30, 0x7F }) {
        u.alarms = alarms;
        Drivers::DriverSettings s = {};
        PS::fromLegacy(u, s);
        Drivers::LegacyDriverUnits w = {};
        PS::toLegacy(s, w);
        EXPECT_TRUE(std::memcmp(&u, &w, sizeof(u)) == 0);         // stall bridges A/B kept apart
    }
}

namespace {
/// A register file standing in for the chip (the engine's Io port)
struct Regs {
    uint32_t r[32] = {};
    bool hiZ = true;
    int statusReads = 0;
    uint32_t readReg(uint8_t a) { return r[a]; }
    void writeReg(uint8_t a, uint32_t v) {
        if (!hiZ && (a == Drivers::PowerStepReg::STEP_MODE || a == Drivers::PowerStepReg::CONFIG)) return;
        r[a] = v;
    }
    bool outputsOff() { ++statusReads; return hiZ; }
    void clearFaults() { ++statusReads; }
};
}

TEST(apply_writes_hiz_only_registers_only_with_the_outputs_off) {
    Drivers::DriverSettings s = {};
    s.microsteps = 32; s.mode = Drivers::DriveMode::VOLTAGE;
    s.duty = { 0.125f, 0.1875f, 0.25f, 0.25f };
    s.accelFsps2 = s.decelFsps2 = 2008.2f; s.maxSpeedFsps = 992.0f;
    s.overcurrentA = 1.0e6f;
    Regs chip;
    chip.hiZ = false;
    chip.r[Drivers::PowerStepReg::STEP_MODE] = 0x07;
    EXPECT_TRUE(PS::apply(chip, s) == Drivers::ApplyResult::NEEDS_OUTPUTS_OFF);
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::KVAL_RUN], 0x30u);     // the rest is in force
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::ACC], 0x8Au);
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::MAX_SPEED], 0x41u);
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::OCD_TH], 0x1Fu);
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::STEP_MODE], 0x07u);    // not yet
    chip.hiZ = true;
    EXPECT_TRUE(PS::apply(chip, s) == Drivers::ApplyResult::APPLIED);
    EXPECT_EQ(chip.r[Drivers::PowerStepReg::STEP_MODE], 0xC5u);    // SYNC 8/fs, voltage, 1/32
    Drivers::DriverSettings back = PS::readBack(chip);
    EXPECT_EQ(back.microsteps, 32u);
    EXPECT_TRUE(back.mode == Drivers::DriveMode::VOLTAGE);
    EXPECT_TRUE(back.duty.run == PS::kvalToDuty(0x30));
    char text[160];
    PS::diagnostics(chip, Harness::DriverDiagView::SUMMARY, text, sizeof text);
    EXPECT_STREQ(text, "STEP_MODE=5 OCD_TH=1F STALL_TH=00 ALARM_EN=00 CONFIG=0000 FS_SPD=3FF");
    PS::diagnostics(chip, Harness::DriverDiagView::REGISTERS, text, sizeof text);
    EXPECT_TRUE(std::string(text).find("KVAL: HOLD=20 RUN=30 ACC=40 DEC=40\nACC=138 DEC=138 MAXSPD=65 POS=0\n") == 0);
}

TEST(the_reset_watch_needs_two_mismatches_in_a_row) {
    Regs chip;
    PS::ResetWatch w;
    w.arm(chip);                                                   // GATECFG1 0: no watch (a fake)
    EXPECT_FALSE(w.check(chip));
    chip.r[Drivers::PowerStepReg::GATECFG1] = 0xC3;
    w.arm(chip);
    chip.r[Drivers::PowerStepReg::GATECFG1] = 0;
    EXPECT_FALSE(w.check(chip));
    chip.r[Drivers::PowerStepReg::GATECFG1] = 0xC3;
    EXPECT_FALSE(w.check(chip));                                   // a glitch
    chip.r[Drivers::PowerStepReg::GATECFG1] = 0;
    EXPECT_FALSE(w.check(chip));
    EXPECT_TRUE(w.check(chip));                                    // a reset
}
