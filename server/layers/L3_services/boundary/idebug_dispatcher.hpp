/**
 * @file idebug_dispatcher.hpp
 * @brief Services:: provider — bring-up/debug command port (H_L2_L3), gated by DBG:UNLOCK
 *
 * Replaces the harness-resident DebugCommandHandler. The L2 parser routes DBG:UNLOCK,
 * DBG:LOCK and every legacy flat debug command here; output goes through the
 * IResponseSink the parser supplies. Locked by default: debug commands are rejected
 * until debugUnlock() succeeds.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "harness/pins/dispatch_result.hpp"
#include "harness/pins/iresponse_sink.hpp"

namespace Services {

/// Outcome of a bring-up command dispatch (F6: the parser turns Locked into ERROR DBG_LOCKED).
enum class DebugDispatchResult : uint8_t { NotClaimed, Handled, Locked };

class IDebugDispatcher {
public:
    virtual ~IDebugDispatcher() = default;

    virtual Harness::ServiceStatus debugUnlock() = 0;
    virtual Harness::ServiceStatus debugLock() = 0;
    virtual bool debugIsUnlocked() const = 0;

    /// Returns true when `cmd` is a debug command (handled, or rejected because locked —
    /// in which case the rejection text has already been written to `out`).
    virtual DebugDispatchResult debugDispatch(const char* cmd, const char* const* args, uint8_t argCount,
                               Harness::IResponseSink& out) = 0;

    /// Diagnostic text for the SPI2 bus (used by the SPI2 status query). Returns bytes written.
    virtual int debugFormatSpi2Diag(char* buf, size_t bufSize) = 0;
};

} // namespace Services
