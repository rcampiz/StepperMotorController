/**
 * @file harness_signal.hpp
 * @brief Common vocabulary for every harness definition (the Altium "harness definition").
 *
 * A harness definition header (harness_<a>_<b>.hpp) bundles every signal that crosses one
 * layer boundary into one named struct, plus a constexpr table describing each signal:
 * who provides it, which way calls flow, sync/async, and which execution context may use it.
 * Nothing here allocates, nothing is virtual, nothing depends on the RTOS.
 *
 * Harness headers are the ONLY cross-layer headers a layer may include (see
 * scripts/arch/check_includes.py). Platform (F_platform/boundary) is a power net and is
 * never listed as a harness signal.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace Harness {

/// Which side of the boundary implements the signal.
enum class Provider : uint8_t { Lower, Upper };

/// Call direction relative to the stack (L1 is top, L5 is bottom).
enum class Flow : uint8_t { Down, Up, Bidir };

/// Timing of the crossing.
enum class Timing : uint8_t { Sync, AsyncQueue, AsyncFlag };

/// Execution contexts allowed to use the signal (bitmask).
enum class Ctx : uint8_t {
    PreScheduler = 1 << 0,   ///< before the scheduler starts (init only)
    Task         = 1 << 1,   ///< any task context
    ISR          = 1 << 2    ///< interrupt context (must be ISR-safe)
};
constexpr uint8_t operator|(Ctx a, Ctx b) { return static_cast<uint8_t>(a) | static_cast<uint8_t>(b); }
constexpr uint8_t operator|(uint8_t a, Ctx b) { return a | static_cast<uint8_t>(b); }

/// Build a ctx bitmask from one Ctx or an already OR-ed mask (table rows use ctx(...)).
constexpr uint8_t ctx(Ctx a) { return static_cast<uint8_t>(a); }
constexpr uint8_t ctx(uint8_t mask) { return mask; }

/// One row of a harness definition table (pure constexpr data).
struct SignalDesc {
    const char* name;        ///< interface, bundle or queue type, e.g. "Board::ISPIBus"
    Provider    provider;
    Flow        flow;
    Timing      timing;
    uint8_t     ctx;         ///< bitmask of Ctx
    const char* ownership;   ///< one line: who owns buffers / lifetime
};

/// A harness definition: identity plus its signal table.
struct HarnessDesc {
    const char*       id;          ///< "L4_L5"
    const char*       upperLayer;  ///< "Drivers"
    const char*       lowerLayer;  ///< "Board"
    const SignalDesc* signals;
    size_t            count;
};

} // namespace Harness
