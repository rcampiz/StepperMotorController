/**
 * @file boundary/iflashimageaccess.hpp
 * @brief Consumer port — Scheduler side of the Services::IFlashImageAccess harness connection
 *
 * Structurally identical to Services::IFlashImageAccess.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Scheduler {

class IFlashImageAccess {
public:
    virtual ~IFlashImageAccess() = default;

    /** @brief Is the flash image service ready? */
    virtual bool isAvailable() const = 0;

    /** @brief Maximum image slots this flash can hold */
    virtual uint32_t maxSlots() const = 0;

    /** @brief Read a chunk of data from a slot */
    virtual bool readSlotChunk(uint32_t slot, uint32_t offset,
                               uint8_t* buf, size_t len) = 0;

    /** @brief Erase the sectors occupied by one image slot */
    virtual bool eraseSlot(uint32_t slot) = 0;
};

} // namespace Scheduler
