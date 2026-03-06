/**
 * @file comms_protocol_init.cpp
 * @brief Implements Protocol::initCommsProtocol() — L2 protocol wiring
 *
 * L2 cascades to L1: calls transport init through harness, then creates
 * ServiceDispatcher and CommandParser. Includes L2 + harness headers only.
 */

#include "L2_protocol/init/comms_protocol_init.hpp"

// L1 transport init through harness (L2 cascades to L1)
#include "harness/pins/transport_init.hpp"

// L2 internal
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/service_dispatcher.hpp"

namespace Protocol {

bool initCommsProtocol(CommsProtocol& out,
                       const Harness::DispatchInterfaces& dispatches,
                       Harness::IDebugCommands* debugCommands,
                       Harness::IByteChannel& channel,
                       Harness::IClock& clock)
{
    // L1 transport init (cascade to L1 through harness)
    Transport::TransportResult transportResult;
    if (!Transport::initTransport(transportResult, Transport::TransportType::VCP_UART,
                                  channel, clock)) {
        return false;
    }

    // L2 forwarding dispatcher — wire all 12 sub-interfaces
    static ServiceDispatcher dispatcher;
    dispatcher.setMotion(dispatches.motion);
    dispatcher.setConfig(dispatches.config);
    dispatcher.setSafety(dispatches.safety);
    dispatcher.setQueue(dispatches.queue);
    dispatcher.setControlMode(dispatches.controlMode);
    dispatcher.setSupervisorTrim(dispatches.supervisorTrim);
    dispatcher.setEncoder(dispatches.encoder);
    dispatcher.setMotorDriver(dispatches.motorDriver);
    dispatcher.setDisplay(dispatches.display);
    dispatcher.setFlash(dispatches.flash);
    dispatcher.setSystem(dispatches.system);
    dispatcher.setTrace(dispatches.trace);

    // Command parser (L2)
    static CommandParser parser(*transportResult.transport, dispatcher);

    // Debug command handler (optional)
    if (debugCommands) {
        parser.setDebugCommands(debugCommands);
    }

    // Populate output
    out.parser = &parser;
    out.transport = transportResult.transport;

    return true;
}

} // namespace Protocol
