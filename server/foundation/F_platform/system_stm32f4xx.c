#include "stm32f401xe.h"

// System clock frequency (default is HSI 16MHz)
uint32_t SystemCoreClock = 16000000;

// System initialization function
void SystemInit(void) {
    // FPU settings (Enable FPU if present)
    #if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
        SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));  // Set CP10 and CP11 Full Access
    #endif

    // Reset the RCC clock configuration to the default reset state
    // Set HSION bit
    RCC->CR |= (uint32_t)0x00000001;

    // Reset CFGR register
    RCC->CFGR = 0x00000000;

    // Reset HSEON, CSSON and PLLON bits
    RCC->CR &= (uint32_t)0xFEF6FFFF;

    // Reset PLLCFGR register
    RCC->PLLCFGR = 0x24003010;

    // Reset HSEBYP bit
    RCC->CR &= (uint32_t)0xFFFBFFFF;

    // Disable all interrupts
    RCC->CIR = 0x00000000;

    // Configure the Vector Table location
    #ifdef VECT_TAB_SRAM
        SCB->VTOR = SRAM_BASE;
    #else
        SCB->VTOR = FLASH_BASE;
    #endif
}

// Update SystemCoreClock variable
void SystemCoreClockUpdate(void) {
    // For now, just use HSI (16MHz)
    SystemCoreClock = 16000000;
}

// Simple delay function (crude, for basic timing)
void delay_ms(uint32_t ms) {
    // Assuming 16MHz clock, roughly 16000 cycles per ms
    // This is very approximate
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 2000; j++) {
            __NOP();
        }
    }
}
