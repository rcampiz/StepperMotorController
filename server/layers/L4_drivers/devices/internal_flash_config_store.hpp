/**
 * @file internal_flash_config_store.hpp
 * @brief Drivers::IConfigStore over one STM32 internal-flash sector (Board::IInternalFlash)
 *
 * Reads are direct memory reads of the mapped sector; erase/program go through L5.
 */
#pragma once
#include "L4_drivers/boundary/iconfig_store.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include <string.h>

namespace Drivers {

class InternalFlashConfigStore : public IConfigStore {
public:
    /// The single blob at `addr` in erase unit `sector` (the board's flash map: the caller
    /// passes them; superseded on the board by InternalFlashJournalStore)
    InternalFlashConfigStore(Board::IInternalFlash& flash, uint32_t addr, uint8_t sector,
                             size_t capacity)
        : m_flash(flash), m_addr(addr), m_sector(sector), m_capacity(capacity) {}

    size_t capacity() const override { return m_capacity; }

    bool erase() override { return m_flash.eraseSector(m_sector); }

    bool write(const uint8_t* data, size_t len) override {
        if (len > m_capacity) return false;
        return m_flash.writeData(m_addr, data, len);
    }

    size_t read(uint8_t* buf, size_t len) const override {
        if (len > m_capacity) len = m_capacity;
        memcpy(buf, reinterpret_cast<const void*>(static_cast<uintptr_t>(m_addr)), len);
        return len;
    }

private:
    Board::IInternalFlash& m_flash;
    uint32_t m_addr;
    uint8_t  m_sector;
    size_t   m_capacity;
};

} // namespace Drivers
