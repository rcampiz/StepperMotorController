/**
 * @file flash_dispatch.hpp
 * @brief L3 implementation of IFlashImageDispatcher
 */

#pragma once

#include "harness/pins/iflash_image_dispatcher.hpp"

namespace Services {

class FlashDispatch : public Harness::IFlashImageDispatcher {
public:
    bool flashIsAvailable() override;
    FlashInfo flashGetInfo() override;
    uint32_t flashMaxSlots() override;
    bool flashEraseSlot(uint32_t slot) override;
    bool flashWriteSlotData(uint32_t slot, uint32_t offset,
                            const uint8_t* data, size_t len) override;
    bool flashReadSlotChunk(uint32_t slot, uint32_t offset,
                            uint8_t* buf, size_t len) override;
    bool flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                  uint8_t* buf, size_t len) override;
    void flashReadSlotChunkFinish() override;
    bool flashEraseAll() override;
    uint32_t flashSlotAddress(uint32_t slot) override;
};

} // namespace Services
