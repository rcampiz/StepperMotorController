/**
 * @file interrupt_guard.hpp
 * @brief RAII interrupt masking for driver-level critical sections
 *
 * Uses ARM CMSIS intrinsics (PRIMASK). No RTOS dependency.
 * Intended for short, bounded driver operations where interrupt
 * jitter must be prevented (e.g., bit-banged SPI timing).
 *
 * NOT for mutual exclusion between execution contexts -- use ILock for that.
 */

#ifndef HARNESS_INTERRUPT_GUARD_HPP
#define HARNESS_INTERRUPT_GUARD_HPP

#include <stdint.h>

class InterruptGuard {
public:
    InterruptGuard() {
        __asm volatile("mrs %0, primask" : "=r"(m_state));
        __asm volatile("cpsid i" ::: "memory");
    }

    ~InterruptGuard() {
        __asm volatile("msr primask, %0" :: "r"(m_state) : "memory");
    }

    // Non-copyable, non-movable
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

private:
    uint32_t m_state;
};

#endif // HARNESS_INTERRUPT_GUARD_HPP
