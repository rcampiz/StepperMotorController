/**
 * @file comms_protocol_init.hpp
 * @brief L2 protocol init — cascades to L1 transport
 *
 * L3 calls this with an already-constructed IByteChannel.
 * L2 passes it to L1 transport init, then creates ServiceDispatcher
 * and CommandParser.
 */

#pragma once

#include "harness/pins/dispatch_interfaces.hpp"

namespace Harness { class ITransport; class IClock; class IDebugCommands;
                    class IByteChannel; class ICommandProcessor; }

namespace Protocol {

struct CommsProtocol {
    Harness::ICommandProcessor* parser = nullptr;
    Harness::ITransport* transport = nullptr;  ///< From L1 cascade
};

/**
 * @brief Initialize L2 protocol stack (cascades to L1 transport)
 *
 * @param out         Output struct with parser and transport
 * @param dispatches  12 sub-dispatcher interface pointers (from L3 init)
 * @param debugCommands Optional debug command handler (from harness)
 * @param channel     Byte channel (constructed by L4, passed from L3)
 * @param clock       Platform clock (injected from L3)
 * @return true on success
 */
bool initCommsProtocol(CommsProtocol& out,
                       const Harness::DispatchInterfaces& dispatches,
                       Harness::IDebugCommands* debugCommands,
                       Harness::IByteChannel& channel,
                       Harness::IClock& clock);

} // namespace Protocol
