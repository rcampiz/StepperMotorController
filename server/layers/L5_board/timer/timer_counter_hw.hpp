/**
 * @file timer_counter_hw.hpp
 * @brief Concrete ITimerCounter for TIM4 encoder mode on STM32F401
 *
 * Wraps TIM4 hardware registers for use by the L4 encoder driver.
 * Handles timer init (encoder mode 3, 16-bit, both edges).
 */
#pragma once

#include "harness/pins/itimer_counter.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace Board {

class TimerCounterTIM4 : public Harness::ITimerCounter {
public:
    /**
     * @brief Initialize TIM4 in encoder mode
     *
     * Enables TIM4 clock, configures encoder mode 3 (count on both edges),
     * sets full 16-bit range, and starts the counter.
     */
    bool init() {
        RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
        volatile uint32_t dummy = RCC->APB1ENR;
        (void)dummy;

        TIM4->CR1 = 0;
        TIM4->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;       // Encoder mode 3
        TIM4->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // IC1=TI1, IC2=TI2
        TIM4->CCER = 0;        // Rising edge, non-inverted
        TIM4->ARR = 0xFFFF;    // Full 16-bit range
        TIM4->CNT = 0;
        TIM4->CR1 = TIM_CR1_CEN;

        return true;
    }

    uint16_t getCount() const override {
        return static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
    }

    void setCount(uint16_t val) override {
        TIM4->CNT = val;
    }

    bool isCountingDown() const override {
        return (TIM4->CR1 & (1U << 4)) != 0;  // DIR bit
    }
};

} // namespace Board
