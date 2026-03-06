/**
 * @file spi_init.cpp
 * @brief L5 implementation of Harness::initSPIManager
 */

#include "L5_board/init/spi_init.hpp"
#include "L5_board/spi/spi_manager.hpp"

bool Harness::initSPIManager(Harness::ILock& spi1Lock, Harness::ILock& spi2Lock)
{
    return Board::g_spiManager.init(spi1Lock, spi2Lock);
}
