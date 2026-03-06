/**
 * @file imicrosecond_clock.hpp
 * @brief Microsecond-resolution hardware timer — boundary contract
 *
 * Abstraction for TIM5-based microsecond counter. ISR-safe, single read.
 * Separate from IClock (which is RTOS-level scheduling).
 */

#pragma once

#include <stdint.h>

namespace Harness {

class IMicrosecondClock {
public:
    virtual ~IMicrosecondClock() = default;

    /** @brief Get current tick in microseconds (ISR-safe, wraps ~71 min) */
    virtual uint32_t getTickUs() const = 0;
};

/// Global accessor (wired by composition root)
void setMicrosecondClock(IMicrosecondClock* clock);
IMicrosecondClock* microsecondClock();

} // namespace Harness
