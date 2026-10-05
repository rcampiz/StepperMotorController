/**
 * @file imicrosecond_clock.hpp
 * @brief Harness connector + global accessor for the Platform microsecond clock
 *
 * The clock is a Platform primitive (F_platform/boundary/imicrosecond_clock.hpp).
 * The accessor is transitional: the Encoder task reads it until migration step 4,
 * after which only Platform injection remains.
 */
#pragma once

#include "F_platform/boundary/imicrosecond_clock.hpp"

namespace Harness {

void setMicrosecondClock(Platform::IMicrosecondClock* clock);
Platform::IMicrosecondClock* microsecondClock();

} // namespace Harness
