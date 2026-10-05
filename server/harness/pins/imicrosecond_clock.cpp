/**
 * @file imicrosecond_clock.cpp
 * @brief Microsecond clock global accessor implementation
 */

#include "harness/pins/imicrosecond_clock.hpp"

namespace Harness {

static Platform::IMicrosecondClock* s_usClock = nullptr;

void setMicrosecondClock(Platform::IMicrosecondClock* clock) { s_usClock = clock; }
Platform::IMicrosecondClock* microsecondClock() { return s_usClock; }

} // namespace Harness
