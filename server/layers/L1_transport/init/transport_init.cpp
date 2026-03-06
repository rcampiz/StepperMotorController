/**
 * @file transport_init.cpp
 * @brief Implements Transport::initTransport() — L1 transport wrapper
 *
 * Wraps an injected IByteChannel in the appropriate L1 transport.
 * No L4 or L5 includes — only L1 types and harness interfaces.
 */

#include "L1_transport/init/transport_init.hpp"

// L1 transports
#include "L1_transport/rtt_transport.hpp"
#include "L1_transport/uart_transport.hpp"

// Tracing (conditional)
#include "harness/trace/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_transport.hpp"
#endif

namespace Transport {

bool initTransport(TransportResult& out, TransportType type,
                   Harness::IByteChannel& channel, Harness::IClock& clock)
{
    Harness::ITransport* transport = nullptr;

    switch (type) {
    case TransportType::VCP_UART: {
        static UartTransport vcpTransport(channel, clock);
        transport = &vcpTransport;
    } break;

    case TransportType::RTT: {
        static RttTransport rttTransport(channel);
        transport = &rttTransport;
    } break;
    }

    if (transport == nullptr) {
        return false;
    }

    if (!transport->init()) {
        return false;
    }

#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedTransport tracedTransport(*transport);
    out.transport = &tracedTransport;
#else
    out.transport = transport;
#endif

    return true;
}

} // namespace Transport
