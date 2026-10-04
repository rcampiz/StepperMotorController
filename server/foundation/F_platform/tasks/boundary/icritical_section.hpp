/**
 * @file boundary/icriticalsection.hpp
 * @brief Consumer port — Scheduler side of the Platform::ICriticalSection harness connection
 *
 * Structurally identical to Platform::ICriticalSection.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

namespace Scheduler {

class ICriticalSection {
public:
    virtual ~ICriticalSection() = default;

    virtual void enter() = 0;
    virtual void exit() = 0;
};

} // namespace Scheduler
