/**
 * @file icommand_processor.hpp
 * @brief Protocol:: provider — the command processor (parser) as seen from above (H_L2_L3)
 *
 * Driven once per Comms tick. L2 owns the transport (H_L1_L2), the line buffer and every
 * byte of host-facing formatting: responses, async events, telemetry lines, joystick
 * events and the boot banner. Nothing above L2 touches bytes or the transport.
 */
#pragma once

#include <stdint.h>
#include "harness/pins/dispatch_stats.hpp"
#include "harness/pins/ui_types.hpp"
#include "harness/pins/telemetry_types.hpp"

struct AsyncEvent;

namespace Protocol {

class ICommandProcessor {
public:
    virtual ~ICommandProcessor() = default;

    /// Read available bytes, parse complete lines, dispatch, respond. Comms task context.
    virtual void process() = 0;

    /// Revert a temporary baud-rate change if its window expired.
    virtual void checkBaudRevert() = 0;

    /// Emit the startup banner.
    virtual void emitBanner() = 0;

    /// Emit an unsolicited event in the current response format (ASCII "!EVT" or JSON).
    virtual void emitEvent(const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms) = 0;

    /// Emit one periodic telemetry line.
    virtual void emitTelemetry(const Protocol::TelemetrySnapshot& snap) = 0;

    /// Emit a joystick edge event (REMOTE UI mode).
    virtual void emitJoystick(UI::JoyDirection dir, bool pressed) = 0;

    /// Copy dispatch statistics (counts + recent commands).
    virtual void getDispatchStats(Harness::DispatchStats& out) const = 0;
};

} // namespace Protocol
