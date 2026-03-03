/**
 * @file sd_flash.cpp
 * @brief ServiceDispatcher — IFlashImageDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/infra/flash_image_service.hpp"

bool ServiceDispatcher::flashIsAvailable() {
    return Services::g_flashImageService.isAvailable();
}

Comms::IFlashImageDispatcher::FlashInfo ServiceDispatcher::flashGetInfo() {
    auto info = Services::g_flashImageService.getInfo();
    return {info.manufacturer, info.memoryType, info.capacityCode,
            info.capacityBytes, info.maxSlots};
}

uint32_t ServiceDispatcher::flashMaxSlots() {
    return Services::g_flashImageService.maxSlots();
}

bool ServiceDispatcher::flashEraseSlot(uint32_t slot) {
    return Services::g_flashImageService.eraseSlot(slot);
}

bool ServiceDispatcher::flashWriteSlotData(uint32_t slot, uint32_t offset,
                                            const uint8_t* data, size_t len) {
    return Services::g_flashImageService.writeSlotData(slot, offset, data, len);
}

bool ServiceDispatcher::flashReadSlotChunk(uint32_t slot, uint32_t offset,
                                            uint8_t* buf, size_t len) {
    return Services::g_flashImageService.readSlotChunk(slot, offset, buf, len);
}

bool ServiceDispatcher::flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                                 uint8_t* buf, size_t len) {
    return Services::g_flashImageService.readSlotChunkStart(slot, offset, buf, len);
}

void ServiceDispatcher::flashReadSlotChunkFinish() {
    Services::g_flashImageService.readSlotChunkFinish();
}

bool ServiceDispatcher::flashEraseAll() {
    return Services::g_flashImageService.eraseAll();
}

uint32_t ServiceDispatcher::flashSlotAddress(uint32_t slot) {
    return Services::g_flashImageService.slotAddress(slot);
}
