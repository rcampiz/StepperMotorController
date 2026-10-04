/**
 * @file boundary/imicrosecondclock.hpp
 * @brief Consumer port — Scheduler side of the Board::IMicrosecondClock harness connection
 *
 * Structurally identical to Board::IMicrosecondClock.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Scheduler {

class IMicrosecondClock {
public:
    virtual ~IMicrosecondClock() = default;

    /** @brief Get current tick in microseconds (ISR-safe, wraps ~71 min) */
    virtual uint32_t getTickUs() const = 0;
};

} // namespace Scheduler
