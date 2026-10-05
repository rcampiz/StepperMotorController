/**
 * @file idebug_access.hpp
 * @brief Drivers:: provider — raw bring-up access for the L3 debug service (H_L3_L4)
 *
 * The board-level register pokes (SPI/GPIO/powerSTEP01 probes) are L5 by nature; L4
 * exposes them as one opaque command port so L3 never names a register. Task context.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "harness/pins/iresponse_sink.hpp"

namespace Drivers {

class IDebugAccess {
public:
    virtual ~IDebugAccess() = default;

    /// True if `cmd` is a raw bring-up command (without executing it).
    virtual bool knowsCommand(const char* cmd) const = 0;

    /// Execute a raw bring-up command. Returns true if `cmd` was recognised.
    virtual bool rawCommand(const char* cmd, const char* const* args, uint8_t argCount,
                            Harness::IResponseSink& out) = 0;

    /// Diagnostic text for SPI2. Returns bytes written.
    virtual int formatSpi2Diag(char* buf, size_t bufSize) = 0;
};

} // namespace Drivers
