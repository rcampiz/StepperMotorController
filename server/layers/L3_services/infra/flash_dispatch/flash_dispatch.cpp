/**
 * @file flash_dispatch.cpp
 * @brief L3 IFlashImageDispatcher implementation
 */

#include "flash_dispatch.hpp"
#include "L3_services/infra/flash_image_service/flash_image_service.hpp"

namespace Services {

bool FlashDispatch::flashIsAvailable() {
    return g_flashImageService.isAvailable();
}

Harness::IFlashImageDispatcher::FlashInfo FlashDispatch::flashGetInfo() {
    auto info = g_flashImageService.getInfo();
    return {info.manufacturer, info.memoryType, info.capacityCode,
            info.capacityBytes, info.maxSlots};
}

uint32_t FlashDispatch::flashMaxSlots() {
    return g_flashImageService.maxSlots();
}

bool FlashDispatch::flashEraseSlot(uint32_t slot) {
    return g_flashImageService.eraseSlot(slot);
}

bool FlashDispatch::flashWriteSlotData(uint32_t slot, uint32_t offset,
                                        const uint8_t* data, size_t len) {
    return g_flashImageService.writeSlotData(slot, offset, data, len);
}

bool FlashDispatch::flashReadSlotChunk(uint32_t slot, uint32_t offset,
                                        uint8_t* buf, size_t len) {
    return g_flashImageService.readSlotChunk(slot, offset, buf, len);
}

bool FlashDispatch::flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                             uint8_t* buf, size_t len) {
    return g_flashImageService.readSlotChunkStart(slot, offset, buf, len);
}

void FlashDispatch::flashReadSlotChunkFinish() {
    g_flashImageService.readSlotChunkFinish();
}

bool FlashDispatch::flashEraseAll() {
    return g_flashImageService.eraseAll();
}

uint32_t FlashDispatch::flashSlotAddress(uint32_t slot) {
    return g_flashImageService.slotAddress(slot);
}

} // namespace Services
