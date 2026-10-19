/**
 * @file test_config_journal.cpp
 * @brief The L4 journal config store over a RAM image of one flash sector: saves append
 *        into blank flash, read() returns the newest record, the pre-journal blob is the
 *        fallback, and an erase is needed only when every slot is used.
 */
#include "minitest.hpp"
#include "L4_drivers/devices/internal_flash_journal_store.hpp"
#include <vector>
#include <cstring>

namespace {

using Drivers::InternalFlashJournalStore;
constexpr uint32_t SECTOR_BYTES = 0x20000;

/// Flash semantics: erase sets 0xFF, programming can only clear bits.
struct RamSectorFlash : Board::IInternalFlash {
    std::vector<uint8_t> image = std::vector<uint8_t>(SECTOR_BYTES, 0xFF);
    int erases = 0, programs = 0;
    static constexpr uint32_t BASE = 0x1000;       // the "flash address" of the image
    bool eraseSector(uint8_t) override { ++erases; std::fill(image.begin(), image.end(), 0xFF); return true; }
    bool writeData(uint32_t addr, const uint8_t* d, size_t n) override {
        ++programs;
        size_t off = static_cast<size_t>(addr - BASE);
        if (off + n > image.size()) return false;
        for (size_t i = 0; i < n; ++i) image[off + i] &= d[i];
        return true;
    }
    /// The STM32F401's layout: journal in the first 124 KB, the legacy blob in the last 4 KB
    static constexpr uint32_t LEGACY_OFFSET = 0x1F000;
    ConfigRegion configRegion() const override { return {BASE, 7, LEGACY_OFFSET, LEGACY_OFFSET}; }
};

struct Rig {
    RamSectorFlash flash;
    InternalFlashJournalStore store{flash, flash.image.data()};
    std::vector<uint8_t> record(uint8_t tag) {
        std::vector<uint8_t> r(InternalFlashJournalStore::SLOT, 0x00);
        r[0] = 0x52; r[1] = 0x54; r[2] = 0x4F; r[3] = 0x4D;   // "MOTR" magic, little-endian
        r[8] = tag;
        return r;
    }
};

} // namespace

TEST(empty_sector_reads_the_legacy_blob_location) {
    Rig r;
    EXPECT_EQ(r.store.used(), 0u);
    EXPECT_EQ(r.store.remaining(), r.store.slots() * InternalFlashJournalStore::SLOT);
    auto legacy = r.record(0x11);
    std::memcpy(r.flash.image.data() + RamSectorFlash::LEGACY_OFFSET, legacy.data(), 64);
    uint8_t out[64];
    EXPECT_EQ(r.store.read(out, 64), 64u);
    EXPECT_EQ(out[8], 0x11);                       // first boot after the change: the old blob
}

TEST(writes_append_and_read_returns_the_newest) {
    Rig r;
    auto a = r.record(1), b = r.record(2);
    EXPECT_TRUE(r.store.write(a.data(), a.size()));
    EXPECT_TRUE(r.store.write(b.data(), b.size()));
    EXPECT_EQ(r.store.used(), 2u);
    EXPECT_EQ(r.flash.erases, 0);
    uint8_t out[64];
    r.store.read(out, 64);
    EXPECT_EQ(out[8], 2);
    EXPECT_EQ(r.flash.image[0 * InternalFlashJournalStore::SLOT + 8], 1);   // the older record is still in slot 0
    EXPECT_EQ(r.store.remaining(), (r.store.slots() - 2) * InternalFlashJournalStore::SLOT);
}

TEST(full_journal_refuses_writes_until_erased) {
    Rig r;
    auto rec = r.record(7);
    for (uint32_t i = 0; i < r.store.slots(); ++i) {
        EXPECT_TRUE(r.store.write(rec.data(), rec.size()));
    }
    EXPECT_EQ(r.store.remaining(), 0u);
    EXPECT_FALSE(r.store.write(rec.data(), rec.size()));   // the manager decides when to erase
    EXPECT_EQ(r.flash.erases, 0);
    EXPECT_TRUE(r.store.erase());
    EXPECT_EQ(r.store.used(), 0u);
    EXPECT_TRUE(r.store.write(rec.data(), rec.size()));
    EXPECT_EQ(r.store.used(), 1u);
}

TEST(a_record_longer_than_a_slot_is_rejected) {
    Rig r;
    std::vector<uint8_t> big(InternalFlashJournalStore::SLOT + 1, 0x00);
    EXPECT_FALSE(r.store.write(big.data(), big.size()));
    EXPECT_EQ(r.store.used(), 0u);
}

/// Another MCU's flash map (e.g. an RP2350: 4 KB erase units, no pre-journal blob): the
/// store sizes itself from the board's region and holds no addresses of its own
struct SmallSectorFlash : RamSectorFlash {
    ConfigRegion configRegion() const override { return {BASE, 3, 0x1000u, 0u}; }
};

TEST(the_journal_sizes_itself_from_the_boards_region) {
    SmallSectorFlash flash;
    InternalFlashJournalStore store{flash, flash.image.data()};
    EXPECT_EQ(store.slots(), 0x1000u / InternalFlashJournalStore::SLOT);   // 32 records per erase
    uint8_t buf[InternalFlashJournalStore::SLOT];
    EXPECT_EQ(store.read(buf, sizeof(buf)), 0u);           // empty and nothing to migrate
    std::vector<uint8_t> rec(InternalFlashJournalStore::SLOT, 0x00);
    rec[8] = 9;
    for (uint32_t i = 0; i < store.slots(); ++i) {
        EXPECT_TRUE(store.write(rec.data(), rec.size()));
    }
    EXPECT_FALSE(store.write(rec.data(), rec.size()));     // full at the region's end
    EXPECT_EQ(flash.programs, 32);
    EXPECT_TRUE(store.erase());
    EXPECT_EQ(store.used(), 0u);
}
