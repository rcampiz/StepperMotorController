/**
 * @file imicrosecond_clock.hpp
 * @brief Harness connector for IMicrosecondClock + global accessors
 *
 * Provider definition owned by Board:: (L5_board/boundary/imicrosecond_clock.hpp).
 * Consumer port owned by Scheduler:: (F_platform/tasks/boundary/imicrosecond_clock.hpp).
 * Global accessor exposes the consumer port type for downstream consumers.
 */

#pragma once

#include "L5_board/boundary/imicrosecond_clock.hpp"
#include "F_platform/tasks/boundary/imicrosecond_clock.hpp"

namespace Harness {

/// Global accessor (wired by composition root, consumer port type)
void setMicrosecondClock(Scheduler::IMicrosecondClock* clock);
Scheduler::IMicrosecondClock* microsecondClock();

} // namespace Harness
