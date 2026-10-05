/**
 * @file tick_timer.cpp
 * @brief Microsecond tick timer service implementation (facade over Platform clock)
 */

#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"

static Platform::IMicrosecondClock* s_usClock = nullptr;

namespace Services {

void TickTimer_Init(Platform::IMicrosecondClock& clock)
{
    s_usClock = &clock;
}

uint32_t TickTimer_GetTick()
{
    return (s_usClock != nullptr) ? s_usClock->getTickUs() : 0;
}

void TickTimer_DelayUs(uint32_t delayUs)
{
    uint32_t start = TickTimer_GetTick();
    while (TickTimer_Elapsed(start) < delayUs) {
        // Busy-wait
    }
}

} // namespace Services
