/**
 * @file internal_flash.hpp
 * @brief L5-internal: STM32F401 flash sector erase / word program helpers
 *
 * Exposed to L4 only through Board::IInternalFlash (harness_l4_l5.hpp).
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Board {

bool internalFlashEraseSector(uint8_t sector);
bool internalFlashWriteData(uint32_t addr, const uint8_t* data, size_t len);

} // namespace Board
