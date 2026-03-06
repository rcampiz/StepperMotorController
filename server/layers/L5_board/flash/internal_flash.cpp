/**
 * @file internal_flash.cpp
 * @brief STM32F401 internal flash operations — L5 board implementation
 *
 * Handles FLASH register access for persistent config storage.
 * Sector erase + 32-bit word programming.
 */

#include "harness/pins/iinternal_flash.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

// FLASH_CR bits
#define FLASH_CR_PG        (1UL << 0)
#define FLASH_CR_SER       (1UL << 1)
#define FLASH_CR_SNB_Pos   3
#define FLASH_CR_PSIZE_1   (1UL << 9)
#define FLASH_CR_STRT      (1UL << 16)
#define FLASH_CR_LOCK      (1UL << 31)

// FLASH_SR bits
#define FLASH_SR_EOP       (1UL << 0)
#define FLASH_SR_SOP       (1UL << 1)
#define FLASH_SR_WRPERR    (1UL << 4)
#define FLASH_SR_PGAERR    (1UL << 5)
#define FLASH_SR_PGPERR    (1UL << 6)
#define FLASH_SR_PGSERR    (1UL << 7)
#define FLASH_SR_BSY       (1UL << 16)

#define FLASH_SR_ERRORS    (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)

static void flashUnlock()
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

static void flashWaitBusy()
{
    while (FLASH->SR & FLASH_SR_BSY) {}
}

static void flashClearErrors()
{
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_ERRORS;
}

bool Harness::internalFlashEraseSector(uint8_t sector)
{
    flashUnlock();
    flashWaitBusy();
    flashClearErrors();

    // Set up sector erase: PSIZE=2 (32-bit), SNB=sector, SER=1
    FLASH->CR = FLASH_CR_PSIZE_1 | (static_cast<uint32_t>(sector) << FLASH_CR_SNB_Pos) | FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;

    flashWaitBusy();

    if (FLASH->SR & FLASH_SR_ERRORS) {
        FLASH->CR |= FLASH_CR_LOCK;
        return false;
    }

    FLASH->CR &= ~FLASH_CR_SER;
    return true;
}

bool Harness::internalFlashWriteData(uint32_t addr, const uint8_t* data, size_t len)
{
    // Flash should already be unlocked from erase

    // Set programming mode (32-bit)
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;

    // Write 32 bits at a time
    const auto* src = reinterpret_cast<const uint32_t*>(data);
    size_t words = (len + 3) / 4;

    for (size_t i = 0; i < words; i++) {
        *reinterpret_cast<volatile uint32_t*>(addr) = src[i];

        flashWaitBusy();

        if (FLASH->SR & FLASH_SR_ERRORS) {
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            return false;
        }

        addr += 4;
    }

    // Disable programming mode and lock flash
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    return true;
}
