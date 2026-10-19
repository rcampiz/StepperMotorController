/**
 * @file istep_pulse_timer.hpp
 * @brief Abstract STEP pulse generator: a hardware timer that puts one pulse on a STEP line
 *        per period and asks, from its update interrupt, how long to wait for the next one.
 *
 * For stepper drivers without a motion engine of their own (a plain STEP/DIR chip: the MCU
 * makes every pulse and the ramp). The timer owns the STEP pin and the pulse width; the L4
 * driver owns DIR, the ramp and the position count (it runs the PulseHandler).
 *
 * On an STM32 this is a timer in PWM mode 1 (CCR = pulse width, ARR = period - 1, preload on)
 * with its update interrupt calling the handler. Nothing on the board implements it yet
 * (driver boundary step 5: the virtual Nucleo does, docs/design/DRIVER_BOUNDARY.md).
 *
 * Contexts:
 *   setHandler()  pre-scheduler, before the first start()
 *   start()       task context (the driver holds its critical section)
 *   stop()        any context, ISR included: no pulse after it returns
 *   the handler   the timer's interrupt, once after every pulse: integer work only, no
 *                 floats, no RTOS calls; a task-context critical section masks it
 */
#pragma once

#include <stdint.h>

namespace Board {

class IStepPulseTimer {
public:
    /// After each pulse, in the timer's interrupt: the ticks to the next pulse; 0 = no more
    /// pulses (the timer stops itself)
    using PulseHandler = uint32_t (*)(void* context);

    virtual ~IStepPulseTimer() = default;

    /// The period's unit, ticks per second
    virtual uint32_t tickHz() const = 0;
    /// The shortest period the timer and the handler sustain, ticks
    virtual uint32_t minPeriodTicks() const = 0;
    virtual void setHandler(PulseHandler handler, void* context) = 0;
    /// The first pulse after `firstPeriodTicks`, then the handler decides
    virtual void start(uint32_t firstPeriodTicks) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};

} // namespace Board
