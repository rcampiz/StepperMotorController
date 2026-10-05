/**
 * @file transport_init.cpp
 * @brief Implements Transport::initTransport() — L1 over the L5 -> L1 byte-channel exception
 */

#include "harness/pins/transport_init.hpp"
#include "harness/defs/harness_l5_l1_bytes.hpp"

// L1 transports
#include "L1_transport/rtt_transport.hpp"
#include "L1_transport/uart_transport.hpp"

namespace Transport {

bool initTransport(TransportResult& out, TransportType type, Platform::IClock& clock)
{
    // The one documented layer skip: host command bytes come straight from L5
    const Harness::L5L1ByteHarness* bytes = Harness::connectL5L1Bytes(
        type == TransportType::RTT ? Harness::CommsChannel::RTT : Harness::CommsChannel::VCP_UART);
    if (bytes == nullptr) {
        return false;
    }

    Transport::ITransport* transport = nullptr;
    switch (type) {
    case TransportType::VCP_UART: {
        static UartTransport vcpTransport(bytes->channel, clock);
        transport = &vcpTransport;
    } break;
    case TransportType::RTT: {
        static RttTransport rttTransport(bytes->channel);
        transport = &rttTransport;
    } break;
    }

    if (transport == nullptr || !transport->init()) {
        return false;
    }
    out.transport = transport;   // raw; the L1<->L2 harness applies the trace tap
    return true;
}

} // namespace Transport
