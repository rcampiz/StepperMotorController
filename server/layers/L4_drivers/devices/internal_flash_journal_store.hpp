/**
 * @file internal_flash_journal_store.hpp
 * @brief Drivers::IConfigStore as an append-only journal over one STM32 internal-flash sector
 *
 * Why: erasing a 128 KB sector stalls every flash-resident instruction fetch for 1-2 s
 * (the whole MCU, ESTOP included). A journal appends each record into blank flash and
 * erases only when the sector is full, which the config manager does at boot (nothing
 * moves) or, at run time, only after it has checked that the motor is stopped.
 *
 * Layout (STM32F401RE sector 7, 0x08060000..0x0807FFFF):
 *   [base, base + LEGACY_OFFSET)   SLOTS records of SLOT bytes, written in order; a blank
 *                                  slot (first word 0xFFFFFFFF) ends the journal
 *   [base + LEGACY_OFFSET, +4 KB)  where InternalFlashConfigStore kept the single blob:
 *                                  read only when the journal is empty (first boot after
 *                                  the change); wiped by the first erase
 *
 * read() returns the newest record (the manager validates magic + CRC); write() appends
 * and fails when the journal is full; remaining() tells the manager when to compact.
 */
#pragma once

#include "L4_drivers/boundary/iconfig_store.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include <string.h>

namespace Drivers {

class InternalFlashJournalStore : public IConfigStore {
public:
    static constexpr uint32_t SECTOR_BASE   = 0x08060000u;
    static constexpr uint8_t  SECTOR        = 7;
    static constexpr uint32_t SLOT          = 64;        // one MotorConfig record
    static constexpr uint32_t LEGACY_OFFSET = 0x1F000u;  // last 4 KB: the pre-journal blob
    static constexpr uint32_t SLOTS         = LEGACY_OFFSET / SLOT;   // 1984 saves per erase

    /// `base` is the flash address handed to IInternalFlash::writeData; reads go through
    /// `mapped`, the same memory as seen by the CPU (on the MCU the sector is memory-mapped
    /// at `base`; a host test passes a RAM image and any `base` its fake flash understands).
    InternalFlashJournalStore(Board::IInternalFlash& flash,
                              uint32_t base = SECTOR_BASE, uint8_t sector = SECTOR,
                              const uint8_t* mapped = nullptr)
        : m_flash(flash), m_base(base), m_sector(sector),
          m_mem(mapped != nullptr ? mapped
                                  : reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(base))) {}

    size_t capacity() const override { return SLOT; }
    size_t remaining() const override { return (SLOTS - used()) * SLOT; }
    bool erase() override { return m_flash.eraseSector(m_sector); }

    bool write(const uint8_t* data, size_t len) override {
        if (len > SLOT) return false;
        const uint32_t n = used();
        if (n >= SLOTS) return false;                     // full: the caller erases first
        return m_flash.writeData(m_base + n * SLOT, data, len);
    }

    size_t read(uint8_t* buf, size_t len) const override {
        if (len > SLOT) len = SLOT;
        const uint32_t n = used();
        const uint32_t off = (n > 0) ? (n - 1) * SLOT : LEGACY_OFFSET;
        memcpy(buf, m_mem + off, len);
        return len;
    }

    /// Records written since the last erase (leading non-blank slots).
    uint32_t used() const {
        uint32_t n = 0;
        while (n < SLOTS && firstWord(n * SLOT) != 0xFFFFFFFFu) {
            ++n;
        }
        return n;
    }

private:
    uint32_t firstWord(uint32_t off) const {
        uint32_t w;
        memcpy(&w, m_mem + off, sizeof(w));
        return w;
    }

    Board::IInternalFlash& m_flash;
    uint32_t       m_base;
    uint8_t        m_sector;
    const uint8_t* m_mem;
};

} // namespace Drivers
