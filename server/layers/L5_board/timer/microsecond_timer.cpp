/**
 * @file microsecond_timer.cpp
 * @brief TIM5 microsecond timer — L5 board implementation
 *
 * Configures TIM5 as a free-running 32-bit counter at 1 MHz.
 * STM32F401: TIM5 on APB1 (timer clock 84 MHz), PSC=83 → 1 µs.
 */

#include "harness/pins/microsecond_timer_init.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace {

class MicrosecondTimerTIM5 : public Harness::IMicrosecondClock {
public:
    void init() {
        // Enable TIM5 clock (APB1)
        RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
        volatile uint32_t dummy = RCC->APB1ENR;
        (void)dummy;

        // Stop timer during configuration
        TIM5->CR1 = 0;

        // Prescaler: 84 MHz / 84 = 1 MHz (1 µs resolution)
        TIM5->PSC = 83;

        // Full 32-bit range (~71 minutes)
        TIM5->ARR = 0xFFFFFFFF;

        // Reset counter
        TIM5->CNT = 0;

        // Generate update event to load prescaler immediately
        TIM5->EGR = TIM_EGR_UG;

        // Clear update flag
        TIM5->SR = 0;

        // Enable counter (up-counter mode)
        TIM5->CR1 = TIM_CR1_CEN;
    }

    uint32_t getTickUs() const override {
        return TIM5->CNT;
    }
};

static MicrosecondTimerTIM5 s_timer;

} // anonymous namespace

Harness::IMicrosecondClock* Harness::initMicrosecondTimer()
{
    s_timer.init();
    return &s_timer;
}
