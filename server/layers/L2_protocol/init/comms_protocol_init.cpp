/**
 * @file comms_protocol_init.cpp
 * @brief Implements Protocol::initCommsProtocol() — L2 protocol wiring
 *
 * L2 cascades to L1: calls transport init through harness, then creates
 * ServiceDispatcher and CommandParser. Includes L2 + harness headers only.
 */

#include "harness/pins/comms_protocol_init.hpp"

// L1 transport init through harness (L2 cascades to L1)
#include "harness/pins/transport_init.hpp"
#include "harness/harness_wiring_map.hpp"

// L2 internal
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/service_dispatcher.hpp"

namespace Protocol {

bool initCommsProtocol(CommsProtocol& out,
                       const Harness::DispatchInterfaces& dispatches,
                       Scheduler::IDebugCommands* debugCommands,
                       Transport::IByteChannel& channel,
                       Platform::IClock& clock)
{
    // L1 transport init (cascade to L1 through harness)
    Transport::TransportResult transportResult;
    if (!Transport::initTransport(transportResult, Transport::TransportType::VCP_UART,
                                  channel, clock)) {
        return false;
    }

    // L2 forwarding dispatcher — wire all 12 sub-interfaces
    // Adapt Services:: provider types to Protocol:: consumer ports
    static ServiceDispatcher dispatcher;
    dispatcher.setMotion(&Harness::adaptMotionDispatcher(*dispatches.motion));
    dispatcher.setConfig(&Harness::adaptMotorConfigDispatcher(*dispatches.config));
    dispatcher.setSafety(&Harness::adaptSafetyDispatcher(*dispatches.safety));
    dispatcher.setQueue(&Harness::adaptQueueDispatcher(*dispatches.queue));
    dispatcher.setControlMode(&Harness::adaptControlModeDispatcher(*dispatches.controlMode));
    dispatcher.setSupervisorTrim(&Harness::adaptSupervisorTrimDispatcher(*dispatches.supervisorTrim));
    dispatcher.setEncoder(&Harness::adaptEncoderDispatcher(*dispatches.encoder));
    dispatcher.setMotorDriver(&Harness::adaptMotorDriverDispatcher(*dispatches.motorDriver));
    dispatcher.setDisplay(&Harness::adaptDisplayDispatcher(*dispatches.display));
    dispatcher.setFlash(&Harness::adaptFlashImageDispatcher(*dispatches.flash));
    dispatcher.setSystem(&Harness::adaptSystemDispatcher(*dispatches.system));
    dispatcher.setTrace(&Harness::adaptTraceDispatcher(*dispatches.trace));

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
