/**
 * @file microsecond_timer_tim5.hpp
 * @brief TIM5 as the Platform microsecond clock (1 MHz, 32-bit, free running, no IRQ)
 */
#pragma once
#include "F_platform/boundary/imicrosecond_clock.hpp"

namespace Platform {

/// Enable TIM5 at 1 MHz and return the clock. Idempotent. Needs the APB1 clock configured.
IMicrosecondClock& initMicrosecondClockTIM5();

} // namespace Platform
