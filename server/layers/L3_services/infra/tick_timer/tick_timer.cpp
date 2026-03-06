/**
 * @file tick_timer.cpp
 * @brief Microsecond tick timer service implementation
 *
 * Delegates TIM5 register access to L5 via Harness::initMicrosecondTimer().
 * This file provides the Services:: API; hardware lives in L5.
 */

#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/microsecond_timer_init.hpp"

// Stored pointer to L5 timer implementation
static Harness::IMicrosecondClock* s_usTimer = nullptr;

namespace Services {

void TickTimer_Init()
{
    s_usTimer = Harness::initMicrosecondTimer();
}

uint32_t TickTimer_GetTick()
{
    return s_usTimer->getTickUs();
}

void TickTimer_DelayUs(uint32_t delayUs)
{
    uint32_t start = TickTimer_GetTick();
    while (TickTimer_Elapsed(start) < delayUs) {
        // Busy-wait
    }
}

} // namespace Services

Harness::IMicrosecondClock* Services::TickTimer_GetMicrosecondClock() {
    return s_usTimer;
}
