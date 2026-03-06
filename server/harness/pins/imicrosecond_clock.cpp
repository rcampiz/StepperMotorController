/**
 * @file imicrosecond_clock.cpp
 * @brief Microsecond clock global accessor implementation
 */

#include "harness/pins/imicrosecond_clock.hpp"

namespace Harness {

static Board::IMicrosecondClock* s_usClock = nullptr;

void setMicrosecondClock(Board::IMicrosecondClock* clock) { s_usClock = clock; }
Board::IMicrosecondClock* microsecondClock() { return s_usClock; }

} // namespace Harness
