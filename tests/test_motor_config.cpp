/**
 * @file test_motor_config.cpp
 * @brief Step 4a: L3 MotorConfigManager persists through Drivers::IConfigStore (H_L3_L4),
 *        never through a direct flash address.
 */
#include "minitest.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/motor_config/motor_config.hpp"
#include <vector>
#include <cstring>

namespace {
struct FakeConfigStore : Drivers::IConfigStore {
    std::vector<uint8_t> blob = std::vector<uint8_t>(4096, 0xFF);   // erased flash reads 0xFF
    int erases = 0, writes = 0;
    size_t capacity() const override { return blob.size(); }
    bool erase() override { ++erases; std::fill(blob.begin(), blob.end(), 0xFF); return true; }
    bool write(const uint8_t* d, size_t n) override {
        if (n > blob.size()) return false;
        ++writes; std::memcpy(blob.data(), d, n); return true;
    }
    size_t read(uint8_t* out, size_t n) const override {
        if (n > blob.size()) n = blob.size();
        std::memcpy(out, blob.data(), n); return n;
    }
};
}

TEST(empty_store_yields_defaults_and_invalid) {
    FakeConfigStore store;
    Services::MotorConfigManager cfg;
    EXPECT_FALSE(cfg.init(store));
    EXPECT_FALSE(cfg.isValid());
}

TEST(save_then_reload_round_trips_through_the_store) {
    FakeConfigStore store;
    {
        Services::MotorConfigManager cfg;
        cfg.init(store);
        cfg.setStepMode(3);
        EXPECT_TRUE(cfg.saveToFlash());
        EXPECT_EQ(store.erases, 1);
        EXPECT_EQ(store.writes, 1);
        EXPECT_TRUE(cfg.isValid());
    }
    Services::MotorConfigManager again;
    EXPECT_TRUE(again.init(store));          // valid blob found
    EXPECT_EQ(again.getStepMode(), 3u);
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
