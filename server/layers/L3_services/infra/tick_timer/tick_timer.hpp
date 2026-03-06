/**
 * @file tick_timer.hpp
 * @brief Microsecond tick timer service using TIM5
 *
 * Provides a monotonic 32-bit microsecond counter for precise timing:
 *   - Synchronization protocol (ARM/START, START_AT)
 *   - Latency measurement (PING/PONG timestamps)
 *   - Telemetry timestamps
 *
 * TIM5 is a 32-bit timer on APB1 (42 MHz with x2 multiplier = 84 MHz).
 * Configured with prescaler 83 for 1 MHz tick rate (1 microsecond resolution).
 * Full 32-bit range gives ~71 minutes before wraparound.
 *
 * Thread-safe: Can be called from any task or ISR context.
 */

#pragma once

#include <stdint.h>

namespace Harness { class IMicrosecondClock; }

namespace Services {

/**
 * @brief Initialize the tick timer (TIM5)
 *
 * Configures TIM5 as a free-running 32-bit counter with 1 microsecond
 * resolution. Must be called once before any other tick timer functions.
 *
 * @note Call from main() before starting FreeRTOS scheduler.
 */
void TickTimer_Init();

/**
 * @brief Get current tick value in microseconds
 *
 * Returns the current 32-bit counter value. This is a monotonic timer
 * that wraps around approximately every 71.6 minutes (2^32 microseconds).
 *
 * @return Current tick count in microseconds
 *
 * @note Thread-safe and ISR-safe (single 32-bit read).
 */
uint32_t TickTimer_GetTick();

/**
 * @brief Get elapsed time since a reference tick
 *
 * Correctly handles counter wraparound using unsigned arithmetic.
 *
 * @param startTick Reference tick value from TickTimer_GetTick()
 * @return Elapsed microseconds since startTick
 *
 * @note Valid for elapsed times up to ~71.6 minutes.
 */
inline uint32_t TickTimer_Elapsed(uint32_t startTick) {
  return TickTimer_GetTick() - startTick;
}

/**
 * @brief Check if a timeout has expired
 *
 * @param startTick Reference tick value
 * @param timeoutUs Timeout in microseconds
 * @return true if timeout has expired
 */
inline bool TickTimer_IsExpired(uint32_t startTick, uint32_t timeoutUs) {
  return TickTimer_Elapsed(startTick) >= timeoutUs;
}

/**
 * @brief Busy-wait delay in microseconds
 *
 * @param delayUs Delay in microseconds
 *
 * @warning This is a busy-wait loop. Use FreeRTOS vTaskDelay() for
 *          longer delays in task context to allow other tasks to run.
 */
void TickTimer_DelayUs(uint32_t delayUs);

/** @brief Get IMicrosecondClock adapter (valid after TickTimer_Init) */
Harness::IMicrosecondClock* TickTimer_GetMicrosecondClock();

} // namespace Services
