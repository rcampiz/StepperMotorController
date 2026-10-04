/**
 * @file boundary/iclock.hpp
 * @brief Consumer port — Services side of the Platform::IClock harness connection
 *
 * Structurally identical to Platform::IClock.
 * The harness adapter bridges the provider to this consumer port.
 */

#ifndef SERVICES_ICLOCK_HPP
#define SERVICES_ICLOCK_HPP

#include <stdint.h>

namespace Services {

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

} // namespace Services

#endif // SERVICES_ICLOCK_HPP
