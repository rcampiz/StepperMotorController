/**
 * @file test_harness_meter.cpp
 * @brief The harness meter: crossings, bytes, time inside and the crossing task are
 *        counted per harness signal, and a metered tap forwards everything unchanged.
 */

#include "minitest.hpp"
#include "harness/trace/harness_meter.hpp"
#include "harness/taps/metered_taps.hpp"

namespace {

struct StepUs : Platform::IMicrosecondClock {
    uint32_t now = 1000;
    uint32_t getTickUs() const override { return now; }
};

uint8_t g_task = 0;
uint8_t taskNow() { return g_task; }

/// A bus that records what reached it
struct FakeBus : Board::ISPIBus {
    int locks = 0, unlocks = 0;
    size_t bytes = 0;
    Board::SPIMode mode = Board::SPIMode::Mode0;
    void lock() override { ++locks; }
    void unlock() override { ++unlocks; }
    void setMode(Board::SPIMode m) override { mode = m; }
    Board::SPIMode getMode() const override { return mode; }
    uint8_t transfer(uint8_t d) override { ++bytes; return static_cast<uint8_t>(d ^ 0xFF); }
    void transfer(const uint8_t*, uint8_t*, size_t len) override { bytes += len; }
    void writeOnly(const uint8_t*, size_t len) override { bytes += len; }
    void writeFill(const uint8_t*, size_t patternLen, uint32_t repeatCount) override { bytes += patternLen * repeatCount; }
};

Harness::Meter::Counters read(Harness::Meter::Net n)
{
    Harness::Meter::Counters c;
    Harness::Meter::read(n, c);
    return c;
}

} // namespace

TEST(crossings_bytes_time_and_task_are_counted_per_signal) {
    StepUs us;
    Harness::Meter::setClock(&us);
    Harness::Meter::setTaskSource(&taskNow);
    const auto before = read(Harness::Meter::L3_L4_MOTOR);

    g_task = 1;                                              // the Motor task
    {
        Harness::Meter::Scope m(Harness::Meter::L3_L4_MOTOR, 4);
        us.now += 250;                                       // 250 us inside the crossing
    }
    g_task = 3;                                              // the Comms task
    Harness::Meter::count(Harness::Meter::L3_L4_MOTOR, 10, 40);

    const auto after = read(Harness::Meter::L3_L4_MOTOR);
    EXPECT_EQ(after.calls - before.calls, 2u);
    EXPECT_EQ(after.bytes - before.bytes, 14u);
    EXPECT_EQ(after.busyUs - before.busyUs, 290u);
    EXPECT_EQ(after.byTask[1] - before.byTask[1], 1u);
    EXPECT_EQ(after.byTask[3] - before.byTask[3], 1u);
    EXPECT_TRUE(Harness::Meter::info(Harness::Meter::L3_L4_MOTOR).harness[0] == 'H');
}

TEST(a_metered_spi_bus_counts_a_session_and_forwards_everything) {
    StepUs us;
    Harness::Meter::setClock(&us);
    FakeBus bus;
    Harness::MeteredSPIBus metered(bus, Harness::Meter::L4_L5_SPI1_LCD);
    const auto before = read(Harness::Meter::L4_L5_SPI1_LCD);

    metered.lock();
    metered.setMode(Board::SPIMode::Mode3);
    EXPECT_EQ(metered.transfer(0x0F), 0xF0);                 // the real answer comes back
    const uint8_t buf[8] = {};
    metered.writeOnly(buf, sizeof buf);
    metered.writeFill(buf, 2, 100);
    us.now += 1200;                                          // bus held 1.2 ms
    metered.unlock();

    const auto after = read(Harness::Meter::L4_L5_SPI1_LCD);
    EXPECT_EQ(bus.locks, 1);
    EXPECT_EQ(bus.unlocks, 1);
    EXPECT_TRUE(bus.mode == Board::SPIMode::Mode3);
    EXPECT_EQ(bus.bytes, 209u);
    EXPECT_EQ(after.calls - before.calls, 1u);               // one lock..unlock session
    EXPECT_EQ(after.bytes - before.bytes, 209u);
    EXPECT_EQ(after.busyUs - before.busyUs, 1200u);
}


