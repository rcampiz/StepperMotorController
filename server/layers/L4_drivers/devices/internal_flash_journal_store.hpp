/**
 * @file internal_flash_journal_store.hpp
 * @brief Drivers::IConfigStore as an append-only journal over one internal-flash erase unit
 *
 * Why: erasing a large sector stalls every flash-resident instruction fetch (STM32F401: a
 * 128 KB sector, 1-2 s, the whole MCU, ESTOP included). A journal appends each record into
 * blank flash and erases only when the region is full, which the config manager does at
 * boot (nothing moves) or, at run time, only after it has checked that the motor is stopped.
 *
 * Where: the board's IInternalFlash::configRegion() (the flash map is L5's, not L4's).
 * Layout:
 *   [base, base + journalBytes)    slots() records of SLOT bytes, written in order; a blank
 *                                  slot (first word 0xFFFFFFFF) ends the journal
 *   [base + legacyOffset, +SLOT)   if legacyOffset != 0: a pre-journal single blob, read
 *                                  only while the journal is empty; wiped by the first erase
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
    static constexpr uint32_t SLOT = 128;       // one MotorConfig record (V4; 64 before)

    /// The region comes from the board (IInternalFlash::configRegion()); its `base` is the
    /// address handed to writeData. Reads go through `mapped`, the same memory as seen by the
    /// CPU (on the MCU the region is memory-mapped at `base`; a host test passes a RAM image).
    explicit InternalFlashJournalStore(Board::IInternalFlash& flash, const uint8_t* mapped = nullptr)
        : m_flash(flash), m_region(flash.configRegion()), m_slots(m_region.journalBytes / SLOT),
          m_mem(mapped != nullptr ? mapped
                                  : reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(m_region.base))) {}

    /// Records the region holds between erases (992 on the STM32F401's sector 7)
    uint32_t slots() const { return m_slots; }

    size_t capacity() const override { return SLOT; }
    size_t remaining() const override { return (m_slots - used()) * SLOT; }
    bool erase() override { return m_flash.eraseSector(m_region.sector); }

    bool write(const uint8_t* data, size_t len) override {
        if (len > SLOT) return false;
        const uint32_t n = used();
        if (n >= m_slots) return false;                   // full: the caller erases first
        return m_flash.writeData(m_region.base + n * SLOT, data, len);
    }

    size_t read(uint8_t* buf, size_t len) const override {
        if (len > SLOT) len = SLOT;
        const uint32_t n = used();
        if (n == 0 && m_region.legacyOffset == 0) return 0;   // empty, nothing to migrate
        const uint32_t off = (n > 0) ? (n - 1) * SLOT : m_region.legacyOffset;
        memcpy(buf, m_mem + off, len);
        return len;
    }

    /// Records written since the last erase (leading non-blank slots).
    uint32_t used() const {
        uint32_t n = 0;
        while (n < m_slots && firstWord(n * SLOT) != 0xFFFFFFFFu) {
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

    Board::IInternalFlash&               m_flash;
    Board::IInternalFlash::ConfigRegion  m_region;
    uint32_t                             m_slots;
    const uint8_t*                       m_mem;
};

} // namespace Drivers
