/**
 * @file imicrosecond_clock.cpp
 * @brief Microsecond clock global accessor implementation
 */

#include "harness/pins/imicrosecond_clock.hpp"

namespace Harness {

static Scheduler::IMicrosecondClock* s_usClock = nullptr;

void setMicrosecondClock(Scheduler::IMicrosecondClock* clock) { s_usClock = clock; }
Scheduler::IMicrosecondClock* microsecondClock() { return s_usClock; }

} // namespace Harness
