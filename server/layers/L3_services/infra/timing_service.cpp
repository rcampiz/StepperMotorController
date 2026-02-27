/**
 * @file timing_service.cpp
 * @brief Transport delay compensation — simple stored value
 *
 * No RTOS, no hardware, no heap.
 */

#include "L3_services/infra/timing_service.hpp"

namespace Services::Timing {

static uint32_t s_transportDelayMs = 0;

void setTransportDelay(uint32_t ms)
{
    if (ms > 1000) ms = 1000;
    s_transportDelayMs = ms;
}

uint32_t getTransportDelay()
{
    return s_transportDelayMs;
}

} // namespace Services::Timing
