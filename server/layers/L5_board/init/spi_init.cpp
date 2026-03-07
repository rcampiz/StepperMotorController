/**
 * @file spi_init.cpp
 * @brief L5 implementation of Harness::initSPIManager
 */

#include "harness/pins/spi_init.hpp"
#include "L5_board/spi/spi_manager.hpp"

bool Harness::initSPIManager(Platform::ILock& spi1Lock, Platform::ILock& spi2Lock)
{
    return Board::g_spiManager.init(spi1Lock, spi2Lock);
}
