/**
 * @file iinternal_flash.hpp
 * @brief Board:: provider interface — internal flash sector erase / program, and where the
 *        board keeps the motor-config journal
 *
 * Reads are plain memory reads at the sector address; only erase and program need
 * register access. Both block for the duration of the flash operation (ms) with
 * interrupts enabled; task or pre-scheduler context only.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Board {

class IInternalFlash {
public:
    virtual ~IInternalFlash() = default;

    virtual bool eraseSector(uint8_t sector) = 0;
    virtual bool writeData(uint32_t addr, const uint8_t* data, size_t len) = 0;

    /// The region set aside for the motor-config journal. The flash map is the board's, so
    /// L4 holds no addresses: another MCU (other sector sizes, another base) answers here.
    struct ConfigRegion {
        uint32_t base;           ///< flash address of the region; reads are plain memory loads there
        uint8_t  sector;         ///< the erase unit eraseSector() takes for it
        uint32_t journalBytes;   ///< bytes from `base` that hold journal records
        uint32_t legacyOffset;   ///< offset of a pre-journal single blob to migrate (0: none)
    };
    virtual ConfigRegion configRegion() const = 0;
};

} // namespace Board
