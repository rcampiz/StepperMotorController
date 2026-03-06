/**
 * @file imicrosecond_clock.cpp
 * @brief Microsecond clock global accessor implementation
 */

#include "harness/pins/imicrosecond_clock.hpp"

namespace Harness {

static IMicrosecondClock* s_usClock = nullptr;

void setMicrosecondClock(IMicrosecondClock* clock) { s_usClock = clock; }
IMicrosecondClock* microsecondClock() { return s_usClock; }

} // namespace Harness
