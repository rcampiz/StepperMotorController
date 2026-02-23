/**
 * @file flash_image_service.hpp
 * @brief Service layer for NOR flash image storage
 *
 * Manages a simple slot-based layout on the SPI NOR flash for storing
 * RGB565 images (240x320, 153600 bytes each).  Provides high-level
 * operations: query info, erase, write, read — all driver-agnostic
 * above the SPIFlash interface.
 *
 * Memory layout:
 *   0x000000 - 0x001FFF  Reserved (device config, 2 sectors)
 *   0x002000 - onwards   Image slots (sector-aligned, 0x26000 each)
 *
 * No RTOS headers.  No display/LCD dependencies.  The command parser
 * handles LCD streaming by reading slot data via readSlotChunk().
 */

#ifndef FLASH_IMAGE_SERVICE_HPP
#define FLASH_IMAGE_SERVICE_HPP

#include <stdint.h>
#include <stddef.h>

// Forward declaration — driver header not needed in the interface
class SPIFlash;

namespace Services {

// ── Layout constants ────────────────────────────────────────────────
constexpr uint32_t FLASH_IMAGE_BASE    = 0x002000;   // First image slot
constexpr uint32_t FLASH_IMAGE_SIZE    = 153600;      // 240 * 320 * 2 bytes
constexpr uint32_t FLASH_SECTOR_SIZE   = 4096;        // 4 KB sector
constexpr uint32_t FLASH_PAGE_SIZE     = 256;          // Page-program size
constexpr uint32_t FLASH_IMAGE_PADDED  = 0x26000;     // 155648 = ceil(153600/4096)*4096

// ── Info structure ──────────────────────────────────────────────────
struct FlashImageInfo {
    uint8_t  manufacturer;
    uint8_t  memoryType;
    uint8_t  capacityCode;
    uint32_t capacityBytes;
    uint32_t maxSlots;
};

// ── Service class ───────────────────────────────────────────────────
class FlashImageService {
public:
    FlashImageService() = default;

    /**
     * @brief Initialise with an SPIFlash driver instance.
     *
     * Reads JEDEC ID, calculates capacity and max slot count.
     * Call once from the composition root (main.cpp).
     *
     * @param flash  Pointer to an initialised SPIFlash driver (not owned).
     * @return true if flash detected and usable.
     */
    bool init(SPIFlash* flash);

    /** @brief Is the service ready? */
    bool isAvailable() const { return m_flash != nullptr && m_maxSlots > 0; }

    /** @brief Get flash info snapshot. */
    FlashImageInfo getInfo() const;

    /** @brief Maximum image slots this flash can hold. */
    uint32_t maxSlots() const { return m_maxSlots; }

    /**
     * @brief Erase the sectors occupied by one image slot.
     * @param slot  Slot index (0-based).
     * @return true on success.
     */
    bool eraseSlot(uint32_t slot);

    /**
     * @brief Write data into a slot at a byte offset.
     *
     * Handles page-program alignment internally.  Caller is responsible
     * for erasing the slot first.
     *
     * @param slot    Slot index.
     * @param offset  Byte offset within the slot (must be page-aligned).
     * @param data    Source buffer.
     * @param len     Bytes to write (must be <= FLASH_PAGE_SIZE).
     * @return true on success.
     */
    bool writeSlotData(uint32_t slot, uint32_t offset,
                       const uint8_t* data, size_t len);

    /**
     * @brief Read a chunk of data from a slot.
     *
     * @param slot    Slot index.
     * @param offset  Byte offset within the slot.
     * @param buf     Destination buffer.
     * @param len     Bytes to read.
     * @return true on success.
     */
    bool readSlotChunk(uint32_t slot, uint32_t offset,
                       uint8_t* buf, size_t len);

    /**
     * @brief Erase all image slots (all sectors from IMAGE_BASE to end of images).
     *
     * This uses 64KB block erase where possible for speed.
     * Can take several seconds for large flash.
     *
     * @return true on success.
     */
    bool eraseAll();

    /** @brief Get the absolute flash address for a slot. */
    uint32_t slotAddress(uint32_t slot) const {
        return FLASH_IMAGE_BASE + slot * FLASH_IMAGE_PADDED;
    }

private:
    SPIFlash* m_flash       = nullptr;
    uint32_t  m_maxSlots    = 0;
    uint32_t  m_capacityBytes = 0;
};

// Global instance (defined in flash_image_service.cpp)
extern FlashImageService g_flashImageService;

} // namespace Services

#endif // FLASH_IMAGE_SERVICE_HPP
