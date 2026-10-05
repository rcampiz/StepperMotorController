/**
 * @file te_sync.cpp
 * @brief LCD tearing-effect synchronisation — EXTI0 on PA0 (X-NUCLEO-GFX01M2 TE pin)
 *
 * The ISR runs at NVIC priority 4, above configMAX_SYSCALL_INTERRUPT_PRIORITY, so it
 * must not call any FreeRTOS API: it only sets a flag. Waiting is a bounded busy-wait
 * on the DWT cycle counter in the caller's task context.
 */

#include "L5_board/display/te_sync.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

// DWT cycle counter (not in the CMSIS device header for this part)
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

static volatile bool s_teFlag = false;

extern "C" void EXTI0_IRQHandler(void)
{
    if (EXTI->PR & (1U << 0)) {
        EXTI->PR = (1U << 0);  // Clear pending (write-1-to-clear)
        s_teFlag = true;
    }
}

namespace {

class TearingSyncEXTI0 : public Board::ITearingSync {
public:
    void init() {
        if (m_initialized) return;
        m_initialized = true;

        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
        SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~0xFU) | 0x0U;  // PA0 -> EXTI0
        EXTI->RTSR |= (1U << 0);    // Rising edge trigger
        EXTI->FTSR &= ~(1U << 0);   // No falling edge
        EXTI->IMR  |= (1U << 0);    // Unmask line 0

        NVIC_SetPriority(EXTI0_IRQn, 4);  // Above FreeRTOS syscall threshold: flag only
        NVIC_EnableIRQ(EXTI0_IRQn);
    }

    void waitForVSync() override {
        s_teFlag = false;
        uint32_t start = DWT->CYCCNT;
        uint32_t timeout = (SystemCoreClock / 1000) * 18;  // 18 ms > one frame @ 60 Hz
        while (!s_teFlag) {
            if ((DWT->CYCCNT - start) >= timeout) break;
        }
    }

private:
    bool m_initialized = false;
};

TearingSyncEXTI0 s_teSync;

} // namespace

Board::ITearingSync& Board::initTearingSync()
{
    s_teSync.init();
    return s_teSync;
}
