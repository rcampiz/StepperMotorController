/**
 * @file itimer_counter.hpp
 * @brief Abstract interface for a hardware timer/counter peripheral
 *
 * Used by L4 device drivers (e.g., encoder) to access timer registers
 * without depending on CMSIS vendor headers directly.
 */
#pragma once

#include <stdint.h>

namespace Harness {

struct ITimerCounter {
    virtual ~ITimerCounter() = default;

    /** Read the current counter value (16-bit). */
    virtual uint16_t getCount() const = 0;

    /** Write the counter value (e.g., reset to 0). */
    virtual void setCount(uint16_t val) = 0;

    /** True if the counter is currently counting down (DIR bit set). */
    virtual bool isCountingDown() const = 0;
};

} // namespace Harness
