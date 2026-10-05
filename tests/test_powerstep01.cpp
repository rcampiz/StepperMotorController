/**
 * @file test_powerstep01.cpp
 * @brief Step 3: the L4 powerSTEP01 driver runs on bare Board:: pins (no adapters).
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "L4_drivers/devices/powerstep01.hpp"

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
    r.motor.run(true, 0x12345);
    const std::vector<uint8_t> expected{0x51, 0x01, 0x23, 0x45};   // RUN|fwd, speed[19:16], [15:8], [7:0]
    EXPECT_TRUE(r.spi.tx == expected);
    EXPECT_EQ(r.spi.mode, Board::SPIMode::Mode3);
    EXPECT_EQ(r.spi.prescaler, Board::SPIPrescaler::Div32);
    EXPECT_EQ(r.spi.locks, 1);
    EXPECT_EQ(r.spi.unlocks, 1);
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
