/**
 * @file iflash_image_access.hpp
 * @brief Interface for flash image storage operations
 *
 * Outbound harness interface: display_task calls through this to
 * FlashImageService (L3 service). Keeps task decoupled from service layer.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Harness {

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

} // namespace Harness
