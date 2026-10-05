/**
 * @file microsecond_timer_tim5.cpp
 * @brief TIM5 microsecond clock — Platform implementation (moved from L5_board/timer)
 *
 * Platform may include CMSIS for low-level runtime support (CODE_PHILOSOPHY §14).
 */
#include "F_platform/hw/microsecond_timer_tim5.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace {

class MicrosecondTimerTIM5 : public Platform::IMicrosecondClock {
public:
    void init() {
        if (m_initialized) return;
        RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
        volatile uint32_t dummy = RCC->APB1ENR;   // clock enable settles
        (void)dummy;
        TIM5->CR1 = 0;
        TIM5->PSC = 83;                 // 84 MHz / (83+1) = 1 MHz
        TIM5->ARR = 0xFFFFFFFF;
        TIM5->CNT = 0;
        TIM5->EGR = TIM_EGR_UG;
        TIM5->SR = 0;
        TIM5->CR1 = TIM_CR1_CEN;
        m_initialized = true;
    }
    uint32_t getTickUs() const override { return TIM5->CNT; }
private:
    bool m_initialized = false;
};

MicrosecondTimerTIM5 s_timer;

} // namespace

Platform::IMicrosecondClock& Platform::initMicrosecondClockTIM5()
{
    s_timer.init();
    return s_timer;
}
