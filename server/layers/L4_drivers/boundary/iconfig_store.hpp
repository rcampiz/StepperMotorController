/**
 * @file iconfig_store.hpp
 * @brief Drivers:: provider interface — persistent configuration blob
 *
 * Consumed by L3 (MotorConfigManager) through harness_l3_l4.hpp. Backed by the STM32
 * internal flash sector via Board::IInternalFlash (harness_l4_l5.hpp).
 * erase()/write() block for the flash operation; task or pre-scheduler context only.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Drivers {

class IConfigStore {
public:
    virtual ~IConfigStore() = default;

    /// Largest blob one write() accepts.
    virtual size_t capacity() const = 0;
    /// Wipe the store. On internal flash this is the sector erase that stalls the MCU.
    virtual bool erase() = 0;
    /// Store a blob. A journal appends and returns false when it is full (erase first);
    /// an overwriting store needs an erase before every write.
    virtual bool write(const uint8_t* data, size_t len) = 0;
    /// Copy up to len bytes of the newest stored blob into buf. Returns bytes copied.
    virtual size_t read(uint8_t* buf, size_t len) const = 0;
    /// Bytes write() can still take without an erase (a journal's free slots).
    virtual size_t remaining() const { return capacity(); }
};

} // namespace Drivers
