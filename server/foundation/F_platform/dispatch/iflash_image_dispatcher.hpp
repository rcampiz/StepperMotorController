/**
 * @file iflash_image_dispatcher.hpp
 * @brief Interface for NOR flash image operations (FLASH_* commands)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Comms {

class IFlashImageDispatcher {
public:
    virtual ~IFlashImageDispatcher() = default;

    static constexpr uint32_t FLASH_IMAGE_SIZE  = 153600;  // 240*320*2
    static constexpr uint32_t FLASH_PAGE_SIZE   = 256;

    struct FlashInfo {
        uint8_t  manufacturer;
        uint8_t  memoryType;
        uint8_t  capacityCode;
        uint32_t capacityBytes;
        uint32_t maxSlots;
    };

    virtual bool flashIsAvailable() = 0;
    virtual FlashInfo flashGetInfo() = 0;
    virtual uint32_t flashMaxSlots() = 0;
    virtual bool flashEraseSlot(uint32_t slot) = 0;
    virtual bool flashWriteSlotData(uint32_t slot, uint32_t offset,
                                    const uint8_t* data, size_t len) = 0;
    virtual bool flashReadSlotChunk(uint32_t slot, uint32_t offset,
                                    uint8_t* buf, size_t len) = 0;
    virtual bool flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                         uint8_t* buf, size_t len) = 0;
    virtual void flashReadSlotChunkFinish() = 0;
    virtual bool flashEraseAll() = 0;
    virtual uint32_t flashSlotAddress(uint32_t slot) = 0;
};

} // namespace Comms
