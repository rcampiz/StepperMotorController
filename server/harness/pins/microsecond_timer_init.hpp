/**
 * @file microsecond_timer_init.hpp
 * @brief Microsecond timer board init — boundary contract
 *
 * L3 tick_timer calls through this to initialize the hardware timer.
 * L5 provides the implementation (TIM5 register access).
 */

#pragma once

namespace Harness {

class IMicrosecondClock;

/** Initialize the hardware microsecond timer. Returns IMicrosecondClock adapter. */
IMicrosecondClock* initMicrosecondTimer();

} // namespace Harness
