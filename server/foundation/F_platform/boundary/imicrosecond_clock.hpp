/**
 * @file imicrosecond_clock.hpp
 * @brief Platform:: primitive — free-running 32-bit microsecond counter
 *
 * Part of the Platform power net (rule 4): any sheet may read it, including ISRs.
 * Backed by TIM5 on the STM32F401 (F_platform/hw/microsecond_timer_tim5.cpp);
 * host tests inject a fake.
 */
#ifndef PLATFORM_IMICROSECOND_CLOCK_HPP
#define PLATFORM_IMICROSECOND_CLOCK_HPP

#include <stdint.h>

namespace Platform {

class IMicrosecondClock {
public:
    virtual ~IMicrosecondClock() = default;

    /// Monotonic microseconds since start, wraps every ~71.6 minutes. ISR-safe.
    virtual uint32_t getTickUs() const = 0;
};

class NullMicrosecondClock : public IMicrosecondClock {
public:
    uint32_t getTickUs() const override { return 0; }
};

} // namespace Platform

#endif // PLATFORM_IMICROSECOND_CLOCK_HPP
