/**
 * @file iinternal_flash.hpp
 * @brief STM32 internal flash operations — boundary contract
 *
 * Abstracts FLASH register access for persistent config storage.
 * L3 motor_config calls these without CMSIS includes.
 * L5 provides the implementation.
 *
 * Separate from IFlash (which is for external SPI NOR flash).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Harness {

/**
 * @brief Erase an internal flash sector
 * @param sector Sector number (0-7 for STM32F401)
 * @return true on success
 */
bool internalFlashEraseSector(uint8_t sector);

/**
 * @brief Write data to internal flash (32-bit aligned)
 * @param addr Target flash address
 * @param data Source data (must be 4-byte aligned)
 * @param len Number of bytes to write (rounded up to 4)
 * @return true on success
 */
bool internalFlashWriteData(uint32_t addr, const uint8_t* data, size_t len);

} // namespace Harness
