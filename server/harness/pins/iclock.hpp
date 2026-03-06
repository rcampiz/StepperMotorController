/**
 * @file iclock.hpp
 * @brief Abstract tick/delay clock interface
 *
 * Platform timing abstraction for task-level scheduling.
 * Implementations may use:
 *   - FreeRTOS tick counter + vTaskDelay
 *   - Bare SysTick counter (superloop)
 *   - Stub (unit test)
 *
 * No RTOS headers may appear in this file.
 *
 * NOTE: This is NOT a replacement for TickTimer (TIM5 microsecond counter).
 * TickTimer is ISR-safe hardware timing in L3/infra. IClock is for
 * RTOS-level scheduling (task delays, coarse timeouts).
 */

#ifndef PLATFORM_ICLOCK_HPP
#define PLATFORM_ICLOCK_HPP

#include <stdint.h>

namespace Harness {

class IClock {
public:
    virtual ~IClock() = default;

    /** @brief Get current system tick in milliseconds */
    virtual uint32_t getTickMs() const = 0;

    /** @brief Get system tick from ISR context (ISR-safe) */
    virtual uint32_t getTickMsFromISR() const = 0;

    /** @brief Delay the current execution context for ms milliseconds */
    virtual void delayMs(uint32_t ms) = 0;

    /**
     * @brief Sleep until the next period boundary (jitter-free periodic loops)
     * @param lastWakeMs Updated each call to track the last wake time
     * @param periodMs   Period in milliseconds
     *
     * RTOS impl: wraps vTaskDelayUntil for drift-free scheduling.
     * Superloop impl: no-op (caller returns immediately).
     */
    virtual void sleepUntilMs(uint32_t& lastWakeMs, uint32_t periodMs) = 0;
};

/**
 * @brief No-op clock for single-threaded / unit test contexts
 *
 * getTickMs() always returns 0. delayMs() is a no-op.
 */
class NullClock : public IClock {
public:
    uint32_t getTickMs() const override { return 0; }
    uint32_t getTickMsFromISR() const override { return 0; }
    void delayMs(uint32_t) override {}
    void sleepUntilMs(uint32_t& /*lastWakeMs*/, uint32_t /*periodMs*/) override {}
};

} // namespace Harness

#endif // PLATFORM_ICLOCK_HPP
