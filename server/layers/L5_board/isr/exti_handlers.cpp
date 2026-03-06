/**
 * @file exti_handlers.cpp
 * @brief EXTI interrupt handlers — L5 board layer
 *
 * ISR handlers that access CMSIS EXTI registers. Each handler
 * clears the pending bit and calls a registered callback.
 *
 * EXTI0 is handled by te_sync.cpp (TE pin on PA0).
 * EXTI9_5 is handled here (encoder index on PC9).
 */

#include "X_vendor/CMSIS/stm32f401xe.h"
#include "harness/pins/exti_callbacks.hpp"

extern "C" void EXTI9_5_IRQHandler(void)
{
    if (EXTI->PR & (1U << 9)) {
        EXTI->PR = (1U << 9);  // Clear pending (write-1-to-clear)
        auto cb = Harness::getExtiCallback(9);
        if (cb) cb();
    }
}
