/**
 * @file itearing_sync.hpp
 * @brief Board:: provider interface — LCD tearing-effect (V-blank) synchronisation
 *
 * Implemented by the EXTI0/PA0 handler in L5_board/display/te_sync.cpp. The ISR runs at
 * NVIC priority 4, above the FreeRTOS syscall threshold, so it only sets a flag; waiting
 * is a bounded busy-wait (one frame, ~18 ms) in the caller's task context.
 */
#pragma once

namespace Board {

class ITearingSync {
public:
    virtual ~ITearingSync() = default;

    /// Block until the next TE rising edge or an 18 ms timeout. Task context only.
    virtual void waitForVSync() = 0;
};

} // namespace Board
