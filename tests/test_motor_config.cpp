/**
 * @file test_motor_config.cpp
 * @brief Step 4a: L3 MotorConfigManager persists through Drivers::IConfigStore (H_L3_L4),
 *        never through a direct flash address. Driver boundary step 3: the record (V5) holds
 *        the driver's settings in physical units; V1-V4 records (powerSTEP01 register values)
 *        are migrated once at load through the driver's own translation.
 */
#include "minitest.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/motor_config/motor_config.hpp"
#include "L4_drivers/devices/powerstep01_settings.hpp"
#include "fakes/powerstep_model.hpp"
#include <vector>
#include <cstring>

namespace {
struct FakeConfigStore : Drivers::IConfigStore {
    std::vector<uint8_t> blob = std::vector<uint8_t>(4096, 0xFF);   // erased flash reads 0xFF
    int erases = 0, writes = 0;
    size_t free_bytes = 4096;                     // journal model: what write() can still take
    size_t capacity() const override { return blob.size(); }
    size_t remaining() const override { return free_bytes; }
    bool erase() override {
        ++erases; free_bytes = blob.size(); std::fill(blob.begin(), blob.end(), 0xFF); return true;
    }
    bool write(const uint8_t* d, size_t n) override {
        if (n > blob.size() || n > free_bytes) return false;
        ++writes; free_bytes -= n; std::memcpy(blob.data(), d, n); return true;
    }
    size_t read(uint8_t* out, size_t n) const override {
        if (n > blob.size()) n = blob.size();
        std::memcpy(out, blob.data(), n); return n;
    }
};

using fakes::PowerStepModel;

/// The bench board's saved V4 config (2026-10-19): TVAL 04/11/14/14, current mode, step mode 7
/// (runs 1/16), idle release 3000 ms, ACC/DEC 344, MAX_SPEED 87, OCD_TH 21, PRED on, TSW 48 us
Services::MotorConfigV4 benchV4(Services::MotorConfigManager& crc)
{
    Services::MotorConfigV4 v{};
    v.magic = Services::MOTOR_CONFIG_MAGIC;
    v.version = 4;
    v.driveMode = 1;
    v.tvalHold = 0x04; v.tvalRun = 0x11; v.tvalAcc = 0x14; v.tvalDec = 0x14;
    v.kvalHold = 0x20; v.kvalRun = 0x30; v.kvalAcc = 0x40; v.kvalDec = 0x40;
    v.ocdThreshold = 21;
    v.stallThreshold = 0x40;                       // the old default (the chip keeps bits 4:0)
    v.acceleration = 344; v.deceleration = 344;
    v.maxSpeed = 87; v.minSpeed = 0; v.fsSpeed = 0x3FF;
    v.faultEnable.ocd = 1; v.faultEnable.thermalSD = 1; v.faultEnable.uvlo = 1;
    v.faultAction = 1;
    v.stepMode = 7;
    v.encFilterType = 0x0F; v.encFilterParam = 200; v.encSmaWindow = 8; v.encMeasWindowMs = 40;
    v.fullStepsPerRev = 200; v.encoderPPR = 4000;
    v.pidKp100 = 120; v.followMoveError = 77;
    v.cmPredEn = 2; v.cmTswPlus1 = 12;            // DRV:CFG:PRED ON 48
    v.idleReleaseMs = 3000;
    v.crc32 = crc.blobCrc(reinterpret_cast<const uint8_t*>(&v), sizeof(v) - sizeof(v.crc32));
    return v;
}
}

TEST(empty_store_yields_defaults_and_invalid) {
    FakeConfigStore store;
    Services::MotorConfigManager cfg;
    EXPECT_FALSE(cfg.init(store));
    EXPECT_FALSE(cfg.isValid());
}

TEST(the_defaults_are_the_bench_proven_ocd_th_2_and_the_lower_motion_currents) {
    // Bench 2026-10-20: OCD_TH 0 (26 A) flags on switching transients while running; 2 is the
    // lowest clean in every soak. Motion currents 25 % under 1.326 / 1.56 A, hold kept.
    FakeConfigStore store;
    Services::MotorConfigManager cfg;
    (void)cfg.init(store);
    const Harness::DriverSettings& d = cfg.getDriverSettings();
    EXPECT_EQ(Drivers::PowerStepSettings::ampsToOcd(d.overcurrentA), 2u);
    EXPECT_NEAR(d.currentA.hold, 0.312, 1e-6);
    EXPECT_NEAR(d.currentA.run, 1.014, 1e-6);
    EXPECT_NEAR(d.currentA.acc, 1.17, 1e-6);
    EXPECT_NEAR(d.currentA.dec, 1.17, 1e-6);
}

TEST(save_then_reload_round_trips_through_the_store) {
    FakeConfigStore store;
    PowerStepModel drv;
    {
        Services::MotorConfigManager cfg;
        cfg.init(store, &drv);
        cfg.setMicrosteps(8);
        Harness::DriverSettings s = cfg.getDriverSettings();
        s.accelFsps2 = 1234.5f;
        cfg.setDriverSettings(s);
        EXPECT_TRUE(cfg.saveToFlash());
        EXPECT_EQ(store.erases, 0);              // a save is an append, never an erase
        EXPECT_EQ(store.writes, 1);
        EXPECT_TRUE(cfg.isValid());
    }
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store, &drv));        // valid blob found
    EXPECT_FALSE(again.migratedLegacy());        // already V5
    EXPECT_EQ(again.getMicrosteps(), 8u);
    EXPECT_TRUE(again.getDriverSettings().accelFsps2 == 1234.5f);   // physical, stored as-is
}

TEST(current_mode_runs_at_most_1_16_and_conversions_follow) {
    // Bench 2026-10-18: 1/128 configured in current mode, the chip ran 1/16, and a 1-rev jog
    // (25600 microsteps) turned the shaft 8 times. Conversions use the resolution in force,
    // and the cap is the driver's (DriverCaps), not a constant in L3.
    FakeConfigStore store;
    PowerStepModel drv;
    Services::MotorConfigManager cfg;
    cfg.init(store, &drv);
    cfg.setMicrosteps(128);
    cfg.setDriveMode(Harness::DriveMode::VOLTAGE);       // voltage: 1/128 as asked
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 128u);
    EXPECT_EQ(cfg.getMicrostepsPerRev(), 25600u);
    cfg.setDriveMode(Harness::DriveMode::CURRENT);       // current: 1/16
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 16u);
    EXPECT_EQ(cfg.getMicrostepsPerRev(), 3200u);
    EXPECT_EQ(cfg.getMicrosteps(), 128u);                 // kept for voltage mode
    cfg.setMicrosteps(8);                                 // 1/8 is within reach in current mode
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 8u);
    cfg.setMicrosteps(128);
    cfg.setDriveMode(Harness::DriveMode::VOLTAGE);       // back to voltage: 1/128 again
    EXPECT_EQ(cfg.getMicrostepsPerRev(), 25600u);
    const Harness::DriverCaps& c = cfg.caps();
    EXPECT_EQ(Harness::effectiveMicrosteps(c, 64, Harness::DriveMode::CURRENT), 16u);
    EXPECT_EQ(Harness::effectiveMicrosteps(c, 512, Harness::DriveMode::VOLTAGE), 128u);
    EXPECT_EQ(Harness::effectiveMicrosteps(c, 48, Harness::DriveMode::VOLTAGE), 32u);   // a power of two
    // Without a driver nothing caps (host tests of the store alone)
    Services::MotorConfigManager bare;
    bare.init(store);
    bare.setMicrosteps(128);
    bare.setDriveMode(Harness::DriveMode::CURRENT);
    EXPECT_EQ(bare.getEffectiveMicrosteps(), 128u);
}

TEST(the_bench_boards_v4_config_migrates_to_the_same_physical_values_and_registers) {
    FakeConfigStore store;
    PowerStepModel drv;
    Services::MotorConfigManager helper;
    const Services::MotorConfigV4 v4 = benchV4(helper);
    std::memcpy(store.blob.data(), &v4, sizeof(v4));

    Services::MotorConfigManager cfg;
    EXPECT_TRUE(cfg.init(store, &drv));
    EXPECT_TRUE(cfg.isValid());
    EXPECT_TRUE(cfg.migratedLegacy());
    EXPECT_EQ(store.writes, 0);                                   // not rewritten until a save
    const Harness::DriverSettings& s = cfg.getDriverSettings();
    // Physical: the same currents, ramps, ceiling, resolution as the board runs today
    EXPECT_TRUE(s.mode == Harness::DriveMode::CURRENT);
    EXPECT_NEAR(s.currentA.hold, 0.312, 1e-6);
    EXPECT_NEAR(s.currentA.run, 1.326, 1e-6);
    EXPECT_NEAR(s.currentA.acc, 1.56, 1e-6);
    EXPECT_NEAR(s.currentA.dec, 1.56, 1e-6);
    EXPECT_NEAR(s.accelFsps2, 344 * 14.552, 1e-2);
    EXPECT_NEAR(s.decelFsps2, 344 * 14.552, 1e-2);
    EXPECT_NEAR(s.maxSpeedFsps, 87 * 15625.0 / 1024.0, 1e-3);    // 1327.5 full steps/s
    EXPECT_NEAR(s.overcurrentA * 0.0012, 0.6875, 1e-5);           // OCD_TH 21: 0.6875 V of VDS
    EXPECT_EQ(s.microsteps, 128u);                                 // asked
    EXPECT_EQ(cfg.getEffectiveMicrosteps(), 16u);                  // in force
    EXPECT_EQ(cfg.getIdleReleaseMs(), 3000u);
    EXPECT_EQ(cfg.getFullStepsPerRev(), 200u);
    EXPECT_EQ(cfg.getEncoderPPR(), 4000u);
    EXPECT_EQ(cfg.getPidKp100(), 120);
    EXPECT_EQ(cfg.getFollowMoveError(), 77);
    EXPECT_EQ(cfg.getFaultAction(), 1u);
    EXPECT_EQ(cfg.getFaultAlarms(), Harness::DriverFault::OVERCURRENT | Harness::DriverFault::THERMAL_SHUTDOWN
                                  | Harness::DriverFault::UNDERVOLTAGE);

    // ... and the driver writes the same registers as before
    const Drivers::PowerStepSettings::RegisterImage r = Drivers::PowerStepSettings::encode(s);
    EXPECT_EQ(r.level[0], 0x04u); EXPECT_EQ(r.level[1], 0x11u);
    EXPECT_EQ(r.level[2], 0x14u); EXPECT_EQ(r.level[3], 0x14u);
    EXPECT_EQ(r.acc, 344u); EXPECT_EQ(r.dec, 344u);
    EXPECT_EQ(r.maxSpeed, 87u);
    EXPECT_EQ(r.minSpeed, 0u); EXPECT_EQ(r.fsSpd, 0x3FFu);
    EXPECT_EQ(r.ocdTh, 21u);
    EXPECT_EQ(r.stallTh, 0u);                                      // 0x40: the chip's 5 bits read 0
    EXPECT_EQ(r.alarmEn, 0x0Bu);
    EXPECT_EQ(r.stepMode, 0xCCu);                                  // SYNC, CM_VM, 1/16
    EXPECT_EQ(r.configHi, 0x8000u | (11u << 10));                 // PRED on, TSW 48 us
    EXPECT_EQ(r.tFast, 0x19u); EXPECT_EQ(r.tonMin, 0x29u); EXPECT_EQ(r.toffMin, 0x29u);

    // Protocol 2 sees the same chip units (DRV:CFG? is unchanged)
    Drivers::LegacyDriverUnits u = {};
    EXPECT_TRUE(cfg.toLegacyUnits(u));
    EXPECT_EQ(u.tval[0], 0x04u); EXPECT_EQ(u.tval[1], 0x11u); EXPECT_EQ(u.tval[3], 0x14u);
    EXPECT_EQ(u.kval[0], 0x20u); EXPECT_EQ(u.kval[1], 0x30u); EXPECT_EQ(u.kval[2], 0x40u);
    EXPECT_EQ(u.acc, 344u); EXPECT_EQ(u.maxSpeed, 87u); EXPECT_EQ(u.ocdTh, 21u);
    EXPECT_EQ(u.stepMode, 7u); EXPECT_EQ(u.predEn, 1u); EXPECT_EQ(u.tsw, 11u);
    EXPECT_EQ(u.alarms, 0x0Bu);

    // Saved as V5 and read back: the same settings, no second migration
    EXPECT_TRUE(cfg.saveToFlash());
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store, &drv));
    EXPECT_FALSE(again.migratedLegacy());
    EXPECT_TRUE(std::memcmp(&again.getDriverSettings(), &s, sizeof(s)) == 0);
}

TEST(a_v4_blob_without_a_translating_driver_keeps_the_non_driver_fields) {
    // A driver for another chip cannot read powerSTEP01 units: its defaults, the rest kept
    FakeConfigStore store;
    Services::MotorConfigManager helper;
    const Services::MotorConfigV4 v4 = benchV4(helper);
    std::memcpy(store.blob.data(), &v4, sizeof(v4));
    Services::MotorConfigManager cfg;
    EXPECT_TRUE(cfg.init(store));
    EXPECT_EQ(cfg.getIdleReleaseMs(), 3000u);
    EXPECT_TRUE(cfg.getDriverSettings().currentA.run == Services::MotorConfigManager::DEFAULT_RUN_A);
}

TEST(a_pre_v4_64_byte_blob_is_migrated_with_regulation_at_chip_defaults) {
    // Forge a V3 blob: the V4 prefix fields with a V3 header and a 60-byte CRC
    FakeConfigStore store;
    PowerStepModel drv;
    Services::MotorConfigManager helper;
    Services::MotorConfigV4 v{};
    v.magic = Services::MOTOR_CONFIG_MAGIC;
    v.version = 3;
    v.driveMode = 1;
    v.tvalRun = 0x11;                                             // others 0: "the default"
    v.maxSpeed = 87;
    v.acceleration = v.deceleration = 0x8A;
    v.stepMode = 7;
    Services::MotorConfigV3 old{};
    std::memcpy(old.prefix, &v, sizeof(old.prefix));
    old.crc32 = helper.blobCrc(old.prefix, sizeof(old.prefix));
    std::memcpy(store.blob.data(), &old, sizeof(old));           // 64 bytes, the rest erased

    Services::MotorConfigManager cfg;
    EXPECT_TRUE(cfg.init(store, &drv));
    EXPECT_TRUE(cfg.isValid());
    EXPECT_TRUE(cfg.migratedLegacy());
    Drivers::LegacyDriverUnits u = {};
    EXPECT_TRUE(cfg.toLegacyUnits(u));
    EXPECT_EQ(u.maxSpeed, 87u);
    EXPECT_EQ(u.acc, 0x8Au);
    EXPECT_EQ(u.stepMode, 7u);
    EXPECT_EQ(u.tval[1], 0x11u);
    EXPECT_EQ(u.tval[0], 0x04u);                                  // 0 = the default hold current
    EXPECT_TRUE(cfg.isCurrentMode());
    EXPECT_EQ(u.predEn, 1u);                                      // the firmware default is on
    EXPECT_EQ(u.tsw, Drivers::PowerStepSettings::CM_DEFAULT_TSW);
    EXPECT_EQ(u.tFast, 0x19u);
    EXPECT_EQ(u.tonMin, 0x29u);
    u.predEn = 1; u.tsw = 20; u.tFast = 0x2A; u.tonMin = 0; u.toffMin = 0;
    EXPECT_TRUE(cfg.setFromLegacyUnits(u));
    EXPECT_TRUE(cfg.saveToFlash());                               // written as a 128-byte V5 record
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store, &drv));
    Drivers::LegacyDriverUnits w = {};
    EXPECT_TRUE(again.toLegacyUnits(w));
    EXPECT_EQ(w.predEn, 1u);
    EXPECT_EQ(w.tsw, 20u);
    EXPECT_EQ(w.tFast, 0x2Au);
    EXPECT_EQ(w.tonMin, 0x29u);                                   // 0 stays "chip default"
}

static bool s_gateOpen = true;
static bool gate() { return s_gateOpen; }

TEST(full_store_is_erased_only_when_the_gate_allows_it) {
    FakeConfigStore store;
    Services::MotorConfigManager cfg;
    cfg.init(store);
    cfg.setEraseGate(gate);
    store.free_bytes = 0;                        // journal full
    s_gateOpen = false;                          // motor moving: the 1-2 s erase must wait
    EXPECT_FALSE(cfg.saveToFlash());
    EXPECT_EQ(cfg.lastSaveStatus(), Services::MotorConfigManager::SaveStatus::StoreFull);
    EXPECT_EQ(store.erases, 0);
    s_gateOpen = true;                           // motor stopped
    EXPECT_TRUE(cfg.saveToFlash());
    EXPECT_EQ(cfg.lastSaveStatus(), Services::MotorConfigManager::SaveStatus::Ok);
    EXPECT_EQ(store.erases, 1);
    EXPECT_EQ(store.writes, 1);
}

TEST(nearly_full_store_is_compacted_at_boot_and_keeps_the_record) {
    FakeConfigStore store;
    {
        Services::MotorConfigManager cfg;
        cfg.init(store);
        cfg.setMicrosteps(64);
        EXPECT_TRUE(cfg.saveToFlash());
    }
    store.free_bytes = sizeof(Services::MotorConfig) * 3;   // fewer than the compaction margin left
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store));
    EXPECT_EQ(store.erases, 1);                  // compacted while nothing moves
    EXPECT_EQ(again.getMicrosteps(), 64u);
    EXPECT_EQ(store.free_bytes, 4096u - sizeof(Services::MotorConfig));   // the record went back in as entry 0
}

TEST(a_migrated_record_compacted_at_boot_goes_back_as_a_valid_v5_record) {
    FakeConfigStore store;
    PowerStepModel drv;
    Services::MotorConfigManager helper;
    const Services::MotorConfigV4 v4 = benchV4(helper);
    std::memcpy(store.blob.data(), &v4, sizeof(v4));
    store.free_bytes = sizeof(Services::MotorConfig) * 3;
    Services::MotorConfigManager cfg;
    EXPECT_TRUE(cfg.init(store, &drv));
    EXPECT_EQ(store.erases, 1);
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store, &drv));
    EXPECT_FALSE(again.migratedLegacy());        // a V5 record with a good CRC
    EXPECT_EQ(again.getIdleReleaseMs(), 3000u);
}

TEST(corrupted_blob_is_rejected) {
    FakeConfigStore store;
    Services::MotorConfigManager cfg;
    cfg.init(store);
    cfg.saveToFlash();
    store.blob[8] ^= 0x5A;                   // flip a byte inside the payload
    Services::MotorConfigManager again;
    EXPECT_FALSE(again.init(store));
}
