/**
 * @file tick_timer.cpp
 * @brief Microsecond tick timer service implementation
 *
 * Uses TIM5 as a free-running 32-bit counter at 1 MHz (1 us resolution).
 *
 * STM32F401 Timer Configuration:
 *   - TIM5 is on APB1 (42 MHz base, but timer clock is 84 MHz when APB1 prescaler > 1)
 *   - Prescaler: 83 (divides 84 MHz by 84 to get 1 MHz)
 *   - Auto-reload: 0xFFFFFFFF (full 32-bit range)
 *   - Mode: Up-counter, free-running
 */

#include "L3_services/infra/tick_timer.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace Services {

void TickTimer_Init()
{
    // Enable TIM5 clock (APB1)
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;

    // Small delay for clock to stabilize
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // Stop timer during configuration
    TIM5->CR1 = 0;

    // Configure prescaler for 1 MHz tick rate
    // Timer clock = 84 MHz (APB1 timer clock when APB1 prescaler != 1)
    // Prescaler = 84 - 1 = 83 -> 84 MHz / 84 = 1 MHz
    TIM5->PSC = 83;

    // Set auto-reload to maximum (full 32-bit range)
    TIM5->ARR = 0xFFFFFFFF;

    // Reset counter
    TIM5->CNT = 0;

    // Generate update event to load prescaler immediately
    TIM5->EGR = TIM_EGR_UG;

    // Clear update flag (set by EGR write)
    TIM5->SR = 0;

    // Enable counter (up-counter mode, no other options)
    TIM5->CR1 = TIM_CR1_CEN;
}

uint32_t TickTimer_GetTick()
{
    // TIM5->CNT is a 32-bit register, single read is atomic on Cortex-M4
    return TIM5->CNT;
}

void TickTimer_DelayUs(uint32_t delayUs)
{
    uint32_t start = TickTimer_GetTick();
    while (TickTimer_Elapsed(start) < delayUs) {
        // Busy-wait
    }
}

} // namespace Services
