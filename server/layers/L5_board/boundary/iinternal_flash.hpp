/**
 * @file iinternal_flash.hpp
 * @brief Board:: provider interface — STM32 internal flash sector erase / word program
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
};

} // namespace Board
