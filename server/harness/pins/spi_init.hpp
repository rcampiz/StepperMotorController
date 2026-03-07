/**
 * @file spi_init.hpp
 * @brief Harness contract for SPI manager initialization
 */

#pragma once

namespace Platform { class ILock; }

namespace Harness {

/// Initialize the board SPI manager with bus arbitration locks
bool initSPIManager(Platform::ILock& spi1Lock, Platform::ILock& spi2Lock);

} // namespace Harness
