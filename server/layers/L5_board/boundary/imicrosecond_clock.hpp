/**
 * @file imicrosecond_clock.hpp
 * @brief Microsecond-resolution hardware timer interface
 *
 * Abstraction for TIM5-based microsecond counter. ISR-safe, single read.
 * Separate from IClock (which is RTOS-level scheduling).
 *
 * Board:: owns this interface. Harness connectors provide global accessors.
 */

#pragma once

#include <stdint.h>

namespace Board {

class IMicrosecondClock {
public:
    virtual ~IMicrosecondClock() = default;

    /** @brief Get current tick in microseconds (ISR-safe, wraps ~71 min) */
    virtual uint32_t getTickUs() const = 0;
};

} // namespace Board
