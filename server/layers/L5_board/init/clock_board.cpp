/**
 * @file clock_board.cpp
 * @brief L5 clock initialization for NUCLEO-F401RE
 *
 * HSE 8 MHz -> PLL -> 84 MHz SYSCLK, APB1 42 MHz, APB2 84 MHz
 */

#include "L5_board/init/clock_board.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

extern uint32_t SystemCoreClock;

namespace Harness {

bool initClockBoard()
{
    // Enable HSE (8 MHz crystal on NUCLEO-F401RE)
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    // Configure PLL: HSE / 8 * 336 / 4 = 84 MHz
    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSE |
                   (8 << RCC_PLLCFGR_PLLM_Pos) |
                   (336 << RCC_PLLCFGR_PLLN_Pos) |
                   (1 << RCC_PLLCFGR_PLLP_Pos));

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    // Flash: 2 wait states for 84 MHz, enable prefetch + caches
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    // Bus dividers: AHB /1, APB1 /2 (42 MHz), APB2 /1 (84 MHz)
    RCC->CFGR = (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2
               | RCC_CFGR_PPRE2_DIV1);
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    SystemCoreClock = 84000000;
    return true;
}

} // namespace Harness
