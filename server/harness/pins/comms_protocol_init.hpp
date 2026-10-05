/**
 * @file comms_protocol_init.hpp
 * @brief Init contract: L2 builds the parser over the L3 dispatchers and the L1 transport
 */
#pragma once

namespace Platform { class IClock; }
namespace Protocol { class ICommandProcessor; }
namespace Harness { struct L3Dispatchers; }

namespace Protocol {

struct CommsProtocol {
    Protocol::ICommandProcessor* parser = nullptr;
};

/// L2 cascades to L1 (initTransport), then builds the ServiceDispatcher
/// bound to `dispatches` and the CommandParser over the harness-connected transport.
bool initCommsProtocol(CommsProtocol& out,
                       const Harness::L3Dispatchers& dispatches,
                       Platform::IClock& clock);

} // namespace Protocol
