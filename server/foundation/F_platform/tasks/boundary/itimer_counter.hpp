/**
 * @file boundary/itimercounter.hpp
 * @brief Consumer port — Scheduler side of the Board::ITimerCounter harness connection
 *
 * Structurally identical to Board::ITimerCounter.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Scheduler {

struct ITimerCounter {
    virtual ~ITimerCounter() = default;

    /** Read the current counter value (16-bit). */
    virtual uint16_t getCount() const = 0;

    /** Write the counter value (e.g., reset to 0). */
    virtual void setCount(uint16_t val) = 0;

    /** True if the counter is currently counting down (DIR bit set). */
    virtual bool isCountingDown() const = 0;
};

} // namespace Scheduler
