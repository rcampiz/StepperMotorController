/**
 * @file te_sync.cpp
 * @brief Tearing effect synchronization — L5 board implementation
 *
 * Manages LCD TE pin (PA0 → EXTI0) and DWT cycle counter for
 * frame-sync timeout. The EXTI0 ISR lives here since the TE flag
 * is local state.
 */

#include "harness/pins/ite_sync.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

// DWT cycle counter definitions (minimal CMSIS may not include core_cm4.h)
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_BASE            (0xE0001000UL)
#define DWT                 ((DWT_Type*)DWT_BASE)
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t CYCCNT;
    volatile uint32_t CPICNT;
    volatile uint32_t EXCCNT;
    volatile uint32_t SLEEPCNT;
    volatile uint32_t LSUCNT;
    volatile uint32_t FOLDCNT;
    volatile uint32_t PCSR;
} DWT_Type;
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)
#endif

#ifndef CoreDebug_DEMCR_TRCENA_Msk
#define CoreDebug_BASE      (0xE000EDF0UL)
#define CoreDebug           ((CoreDebug_Type*)CoreDebug_BASE)
typedef struct {
    volatile uint32_t DHCSR;
    volatile uint32_t DCRSR;
    volatile uint32_t DCRDR;
    volatile uint32_t DEMCR;
} CoreDebug_Type;
#define CoreDebug_DEMCR_TRCENA_Msk  (1UL << 24)
#endif

extern uint32_t SystemCoreClock;

// TE flag — set by EXTI0 ISR, cleared by waitForTearingEffect()
static volatile bool s_teFlag = false;

extern "C" void EXTI0_IRQHandler(void)
{
    if (EXTI->PR & (1U << 0)) {
        EXTI->PR = (1U << 0);  // Clear pending (write-1-to-clear)
        s_teFlag = true;
    }
}

void Harness::initTearingEffectSync()
{
    // Enable DWT cycle counter (idempotent if already enabled)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // Configure EXTI0 on PA0 (TE pin) — rising edge
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~0xFU) | 0x0U;  // PA0 → EXTI0
    EXTI->RTSR |= (1U << 0);    // Rising edge trigger
    EXTI->FTSR &= ~(1U << 0);   // No falling edge
    EXTI->IMR  |= (1U << 0);    // Unmask line 0
    NVIC_SetPriority(EXTI0_IRQn, 4);  // Above FreeRTOS syscall threshold
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void Harness::waitForTearingEffect()
{
    s_teFlag = false;
    uint32_t start = DWT->CYCCNT;
    uint32_t timeout = (SystemCoreClock / 1000) * 18;  // 18ms > one frame @ 60Hz
    while (!s_teFlag) {
        if ((DWT->CYCCNT - start) >= timeout) break;
    }
}
