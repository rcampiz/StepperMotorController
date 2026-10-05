/**
 * @file comms_protocol_init.cpp
 * @brief Implements Protocol::initCommsProtocol() — L2 protocol wiring
 *
 * L2 cascades to L1 through the transport init contract, connects H_L1_L2, binds the
 * ServiceDispatcher to the L3 dispatcher bundle (H_L2_L3) and builds the CommandParser.
 * Includes L2 + harness headers only.
 */

#include "harness/pins/comms_protocol_init.hpp"
#include "harness/pins/transport_init.hpp"
#include "harness/defs/harness_l1_l2.hpp"
#include "harness/defs/harness_l2_l3.hpp"

// L2 internal
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/service_dispatcher.hpp"

namespace Protocol {

bool initCommsProtocol(CommsProtocol& out,
                       const Harness::L3Dispatchers& dispatches,
                       Platform::IClock& clock)
{
    // L1 transport init (cascade to L1 through the init contract)
    Transport::TransportResult transportResult;
    if (!Transport::initTransport(transportResult, Transport::TransportType::VCP_UART, clock)) {
        return false;
    }

    // H_L1_L2 (applies the transport trace tap once)
    const Harness::L1L2Harness& l1l2 = Harness::connectL1L2(*transportResult.transport);

    // L2 forwarding composite over the thirteen L3 dispatchers (H_L2_L3)
    static ServiceDispatcher dispatcher;
    dispatcher.bind(dispatches);

    // Command parser (L2)
    static CommandParser parser(l1l2.transport, dispatcher);

    out.parser = &parser;
    return true;
}

} // namespace Protocol
