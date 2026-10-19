/**
 * @file imicrosecond_clock.cpp
 * @brief Microsecond clock global accessor implementation
 */

#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/trace/harness_meter.hpp"

namespace Harness {

static Platform::IMicrosecondClock* s_usClock = nullptr;

void setMicrosecondClock(Platform::IMicrosecondClock* clock)
{
    s_usClock = clock;
    Meter::setClock(clock);   // the harness meter times crossings on the same clock
}
Platform::IMicrosecondClock* microsecondClock() { return s_usClock; }

} // namespace Harness
