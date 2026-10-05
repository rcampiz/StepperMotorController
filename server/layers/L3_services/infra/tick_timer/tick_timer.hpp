/**
 * @file tick_timer.hpp
 * @brief Microsecond tick timer service (L3 facade over Platform::IMicrosecondClock)
 *
 * The hardware counter (TIM5) is a Platform primitive injected at init; this facade
 * keeps the Services:: call sites (trace, command queue, system dispatch) unchanged.
 */
#pragma once

#include <stdint.h>

namespace Platform { class IMicrosecondClock; }

namespace Services {

/** @brief Bind the service to the Platform microsecond clock (call once, pre-scheduler) */
void TickTimer_Init(Platform::IMicrosecondClock& clock);

/** @brief Current microsecond tick (wraps every ~71.6 min). ISR-safe. */
uint32_t TickTimer_GetTick();

/** @brief Microseconds elapsed since startTick (wrap-safe) */
inline uint32_t TickTimer_Elapsed(uint32_t startTick) {
  return TickTimer_GetTick() - startTick;
}

/** @brief True once timeoutUs have elapsed since startTick */
inline bool TickTimer_IsExpired(uint32_t startTick, uint32_t timeoutUs) {
  return TickTimer_Elapsed(startTick) >= timeoutUs;
}

/** @brief Busy-wait for delayUs microseconds */
void TickTimer_DelayUs(uint32_t delayUs);

} // namespace Services
