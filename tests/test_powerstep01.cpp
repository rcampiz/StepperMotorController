/**
 * @file test_powerstep01.cpp
 * @brief Step 3: the L4 powerSTEP01 driver runs on bare Board:: pins (no adapters).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "L4_drivers/devices/powerstep01.hpp"
#include <cmath>
#include <limits>

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
