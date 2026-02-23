/**
 * @file flash_image_service.cpp
 * @brief Flash image storage service implementation
 */

#include "services/flash_image_service.hpp"
#include "drivers/flash_nor.hpp"

namespace Services {

// Global instance
FlashImageService g_flashImageService;

bool FlashImageService::init(SPIFlash* flash) {
    if (flash == nullptr) {
        return false;
    }

    m_flash = flash;

    // Read capacity from JEDEC ID
    m_capacityBytes = flash->capacityBytes();
    if (m_capacityBytes == 0) {
        m_flash = nullptr;
        return false;
    }

    // Calculate how many image slots fit after the reserved region
    uint32_t usableBytes = m_capacityBytes - FLASH_IMAGE_BASE;
    m_maxSlots = usableBytes / FLASH_IMAGE_PADDED;

    return m_maxSlots > 0;
}

FlashImageInfo FlashImageService::getInfo() const {
    FlashImageInfo info = {};
    if (m_flash == nullptr) {
        return info;
    }

    auto jedec = m_flash->readJEDEC();
    info.manufacturer = jedec.manufacturer;
    info.memoryType   = jedec.memoryType;
    info.capacityCode = jedec.capacity;
    info.capacityBytes = m_capacityBytes;
    info.maxSlots     = m_maxSlots;
    return info;
}

bool FlashImageService::eraseSlot(uint32_t slot) {
    if (m_flash == nullptr || slot >= m_maxSlots) {
        return false;
    }

    uint32_t addr = slotAddress(slot);
    uint32_t sectorsNeeded = (FLASH_IMAGE_SIZE + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

    for (uint32_t i = 0; i < sectorsNeeded; i++) {
        m_flash->sectorErase(addr + i * FLASH_SECTOR_SIZE);
    }

    return true;
}

bool FlashImageService::writeSlotData(uint32_t slot, uint32_t offset,
                                       const uint8_t* data, size_t len) {
    if (m_flash == nullptr || slot >= m_maxSlots) {
        return false;
    }
    if (data == nullptr || len == 0 || len > FLASH_PAGE_SIZE) {
        return false;
    }
    if (offset + len > FLASH_IMAGE_SIZE) {
        return false;
    }

    uint32_t addr = slotAddress(slot) + offset;
    m_flash->pageProgram(addr, data, len);
    return true;
}

bool FlashImageService::readSlotChunk(uint32_t slot, uint32_t offset,
                                       uint8_t* buf, size_t len) {
    if (m_flash == nullptr || slot >= m_maxSlots) {
        return false;
    }
    if (buf == nullptr || len == 0) {
        return false;
    }
    if (offset + len > FLASH_IMAGE_SIZE) {
        return false;
    }

    uint32_t addr = slotAddress(slot) + offset;
    m_flash->read(addr, buf, len);
    return true;
}

bool FlashImageService::eraseAll() {
    if (m_flash == nullptr || m_maxSlots == 0) {
        return false;
    }

    // Calculate total region to erase
    uint32_t totalBytes = m_maxSlots * FLASH_IMAGE_PADDED;
    uint32_t addr = FLASH_IMAGE_BASE;
    uint32_t end  = FLASH_IMAGE_BASE + totalBytes;

    // Use 64KB block erase where aligned, else 4KB sector erase
    while (addr < end) {
        if ((addr & 0xFFFF) == 0 && (end - addr) >= 0x10000) {
            // 64KB-aligned and enough space: use block erase
            m_flash->blockErase64(addr);
            addr += 0x10000;
        } else {
            m_flash->sectorErase(addr);
            addr += FLASH_SECTOR_SIZE;
        }
    }

    return true;
}

} // namespace Services
