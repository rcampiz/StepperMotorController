/**
 * @file spi_init.hpp
 * @brief Harness contract for SPI manager initialization
 */

#pragma once

namespace Harness {

class ILock;

/// Initialize the board SPI manager with bus arbitration locks
bool initSPIManager(ILock& spi1Lock, ILock& spi2Lock);

} // namespace Harness
