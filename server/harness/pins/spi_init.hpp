/**
 * @file spi_init.hpp
 * @brief Harness contract for SPI manager initialization
 */

#pragma once

namespace Platform { class ILock; }

namespace Harness {

/// L5 implements: initialize the board SPI manager with bus arbitration locks
bool initSPIManager(Platform::ILock& spi1Lock, Platform::ILock& spi2Lock);

/// Harness implements (harness/defs/harness_connect.cpp): applies the TracedLock taps to the two
/// Platform mutexes and calls initSPIManager. The composition root calls this, never initSPIManager.
bool connectSPIManager(Platform::ILock& spi1Raw, Platform::ILock& spi2Raw);

} // namespace Harness
